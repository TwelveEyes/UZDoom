/*
** vk_levelmesh.cpp
**
** Vulkan resources of the GPU-driven levelmesh
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Bindless textures: one variable-count, partially bound, update-after-bind
** sampled-image array. Each material gets a contiguous block of slots, one
** per layer, on demand; samplers are a separate array indexed by clamp mode.
** ZVulkan does not enable descriptorBindingUpdateUnusedWhilePending, so all
** descriptor writes happen in BeginFrame, when the previous frame's command
** buffers have completed (VulkanRenderDevice::Update waits for every fence).
** A texture requested during a frame becomes visible in the next one.
**
** Material lifetime: VkMaterial::DeleteDescriptors (precache, filter
** change, texture cleanup, material destruction) frees the material's
** slots; the translation table entries that pointed to them fall back to the
** null slot and are resolved again at the next frame start.
*/

// HEADER FILES ------------------------------------------------------------

#include <algorithm>
#include <cstring>

#include "vk_levelmesh.h"
#include "vk_descriptorset.h"
#include "vulkan/shaders/vk_shader.h"
#include "vulkan/system/vk_renderdevice.h"
#include "vulkan/system/vk_commandbuffer.h"
#include "vulkan/textures/vk_hwtexture.h"
#include "vulkan/textures/vk_samplers.h"
#include "vulkan/textures/vk_texture.h"
#include <zvulkan/vulkanbuilders.h>
#include "texturemanager.h"
#include "hw_material.h"
#include "c_cvars.h"
#include "printf.h"
#include "i_time.h"

// PUBLIC DATA DEFINITIONS -------------------------------------------------

// Testing: comma-separated required features to treat as missing ("all" for every one).
CVAR(String, vk_levelmesh_forcemissing, "", 0)

// PRIVATE DATA DEFINITIONS ------------------------------------------------

namespace
{

constexpr uint32_t MAX_BINDLESS = 65536;
constexpr uint32_t MIN_BINDLESS = 4096;
constexpr uint32_t BINDLESS_MARGIN = 256;		// left for the classic sets in the same pipeline layout
constexpr size_t MIN_RING_SIZE = 256 * 1024;

const char *BufferName(int b)
{
	static const char *names[] = { "vertices", "indices", "surfaces", "clusters", "cluster sectors", "cluster surfaces", "sector state", "side state", "plane xform", "texture table" };
	return names[b];
}

bool ForcedMissing(const char *name)
{
	const char *list = *vk_levelmesh_forcemissing;
	if (list == nullptr || *list == 0) return false;
	FString s = list;
	for (auto &tok : s.Split(","))
	{
		tok.StripLeftRight();
		if (tok.CompareNoCase("all") == 0 || tok.CompareNoCase(name) == 0) return true;
	}
	return false;
}

} // namespace

//==========================================================================
//
// Capability
//
//==========================================================================

VkLevelMesh::VkLevelMesh(VulkanRenderDevice *fb) : fb(fb)
{
	auto dev = fb->device.get();
	auto &f = dev->EnabledFeatures;
	auto check = [&](bool ok, const char *name) { if (!ok) mMissing.push_back(name); };

	check(dev->SupportsExtension(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME) && vkCmdDrawIndexedIndirectCountKHR != nullptr, "VK_KHR_draw_indirect_count");
	check(f.Features.multiDrawIndirect, "multiDrawIndirect");
	bool di = dev->SupportsExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
	check(di, "VK_EXT_descriptor_indexing");
	if (di)
	{
		check(f.DescriptorIndexing.runtimeDescriptorArray, "runtimeDescriptorArray");
		check(f.DescriptorIndexing.descriptorBindingPartiallyBound, "descriptorBindingPartiallyBound");
		check(f.DescriptorIndexing.descriptorBindingVariableDescriptorCount, "descriptorBindingVariableDescriptorCount");
		check(f.DescriptorIndexing.shaderSampledImageArrayNonUniformIndexing, "shaderSampledImageArrayNonUniformIndexing");
		check(f.DescriptorIndexing.descriptorBindingSampledImageUpdateAfterBind, "descriptorBindingSampledImageUpdateAfterBind");

		auto &p = dev->PhysicalDevice.Properties.DescriptorIndexing;
		uint32_t limit = std::min({ p.maxDescriptorSetUpdateAfterBindSampledImages, p.maxPerStageDescriptorUpdateAfterBindSampledImages, p.maxPerStageUpdateAfterBindResources });
		mBindlessCapacity = limit > BINDLESS_MARGIN ? std::min(limit - BINDLESS_MARGIN, MAX_BINDLESS) : 0;
		check(mBindlessCapacity >= MIN_BINDLESS, "bindlessCapacity");
	}
}

VkLevelMesh::~VkLevelMesh()
{
	// Sets must go before their pools: the delete list frees descriptor sets first.
	ReleaseLevel();
	auto deleteList = fb->GetCommands()->DrawDeleteList.get();
	deleteList->Add(std::move(mTextureSet));
	deleteList->Add(std::move(mSetPool));
	deleteList->Add(std::move(mTexturePool));
	for (auto &slot : mRing)
	{
		if (slot.buffer) slot.buffer->Unmap();
		deleteList->Add(std::move(slot.buffer));
	}
}

bool VkLevelMesh::Supported(bool report)
{
	std::string missing;
	auto add = [&](const std::string &name) { if (!missing.empty()) missing += ", "; missing += name; };
	for (auto &name : mMissing) add(name);
	static const char *all[] = { "VK_KHR_draw_indirect_count", "multiDrawIndirect", "VK_EXT_descriptor_indexing", "runtimeDescriptorArray",
		"descriptorBindingPartiallyBound", "descriptorBindingVariableDescriptorCount", "shaderSampledImageArrayNonUniformIndexing",
		"descriptorBindingSampledImageUpdateAfterBind", "bindlessCapacity" };
	for (auto name : all)
	{
		if (ForcedMissing(name) && std::find(mMissing.begin(), mMissing.end(), name) == mMissing.end())
			add(std::string(name) + " (forced)");
	}
	if (missing.empty()) return true;
	if (report && missing != mReported)
	{
		Printf("Levelmesh: this Vulkan device lacks %s; gl_uselevelmesh uses the classic renderer.\n", missing.c_str());
		mReported = missing;
	}
	return false;
}

//==========================================================================
//
// Device objects
//
//==========================================================================

void VkLevelMesh::CreateDeviceObjects()
{
	if (mCreated) return;
	mCreated = true;
	auto dev = fb->device.get();
	VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

	DescriptorSetLayoutBuilder setBuilder;
	for (int b = 0; b < LMBIND_NUMBUFFERS; b++)
		setBuilder.AddBinding(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages);
	setBuilder.AddBinding(LMBIND_SAMPLERS, VK_DESCRIPTOR_TYPE_SAMPLER, NUMSAMPLERS, VK_SHADER_STAGE_FRAGMENT_BIT);
	mSetLayout = setBuilder.DebugName("VkLevelMesh.SetLayout").Create(dev);

	mTextureSetLayout = DescriptorSetLayoutBuilder()
		.Flags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
		.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, mBindlessCapacity, VK_SHADER_STAGE_FRAGMENT_BIT,
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
		.DebugName("VkLevelMesh.TextureSetLayout")
		.Create(dev);

	// A level change allocates the new set before the old one is deleted.
	mSetPool = DescriptorPoolBuilder()
		.MaxSets(4)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, LMBIND_NUMBUFFERS * 4)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_SAMPLER, NUMSAMPLERS * 4)
		.DebugName("VkLevelMesh.SetPool")
		.Create(dev);

	mTexturePool = DescriptorPoolBuilder()
		.Flags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
		.MaxSets(1)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, mBindlessCapacity)
		.DebugName("VkLevelMesh.TexturePool")
		.Create(dev);
	mTextureSet = mTexturePool->allocate(mTextureSetLayout.get(), mBindlessCapacity);
	mTextureSet->SetDebugName("VkLevelMesh.TextureSet");

	auto descriptors = fb->GetDescriptorSetManager();
	mPipelineLayout = PipelineLayoutBuilder()
		.AddSetLayout(descriptors->GetFixedSetLayout())
		.AddSetLayout(descriptors->GetHWBufferSetLayout())
		.AddSetLayout(mSetLayout.get())
		.AddSetLayout(mTextureSetLayout.get())
		.AddPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants))
		.DebugName("VkLevelMesh.PipelineLayout")
		.Create(dev);

	// Slot 0: the null texture.
	WriteSlot(0, fb->GetTextureManager()->GetNullTextureView()->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	FlushSlotWrites();
}

//==========================================================================
//
// Level upload / release
//
//==========================================================================

void VkLevelMesh::ReleaseLevel()
{
	auto deleteList = fb->GetCommands()->DrawDeleteList.get();
	for (auto &buf : mBuffers) deleteList->Add(std::move(buf));
	deleteList->Add(std::move(mSet));
	for (auto &s : mSizes) s = 0;
	for (auto &c : mCounts) c = 0;
	mResident = false;
	mId = 0;
	mTable.clear();
	mPendingFlag.clear();
	mPending.clear();
	mTableDirtyMin = ~0u;
	mTableDirtyMax = 0;
	// Material slots stay cached across levels; only the table references go.
	for (auto &it : mMaterials) it.second.ids.clear();
}

void VkLevelMesh::SetData(const LevelMeshUploadData *data)
{
	ReleaseLevel();
	if (data == nullptr || !Supported(false)) return;

	double t0 = I_msTimeF();
	CreateDeviceObjects();
	auto dev = fb->device.get();

	struct Src { const void *data; size_t size; unsigned count; VkBufferUsageFlags usage; };
	Src src[LMBIND_NUMBUFFERS] = {
		{ data->vertices.data, data->vertices.size * sizeof(LevelMeshVertex), data->vertices.size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT },
		{ data->indices.data, data->indices.size * sizeof(uint32_t), data->indices.size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT },
		{ data->surfaces.data, data->surfaces.size * sizeof(LevelMeshSurface), data->surfaces.size, 0 },
		{ data->clusters.data, data->clusters.size * sizeof(LevelMeshCluster), data->clusters.size, 0 },
		{ data->clusterSectors.data, data->clusterSectors.size * sizeof(uint32_t), data->clusterSectors.size, 0 },
		{ data->clusterSurfaces.data, data->clusterSurfaces.size * sizeof(uint32_t), data->clusterSurfaces.size, 0 },
		{ data->sectors.data, data->sectors.size * sizeof(LevelMeshSectorState), data->sectors.size, 0 },
		{ data->sides.data, data->sides.size * sizeof(LevelMeshSideState), data->sides.size, 0 },
		{ data->planeXforms.data, data->planeXforms.size * sizeof(LevelMeshPlaneXform), data->planeXforms.size, 0 },
		{ nullptr, data->translationTableSize * sizeof(LevelMeshTextureEntry), data->translationTableSize, 0 },
	};

	auto cmd = fb->GetCommands()->GetTransferCommands();
	BufferTransfer transfer;
	bool anyTransfer = false;
	for (int b = 0; b < LMBIND_NUMBUFFERS; b++)
	{
		size_t size = std::max(src[b].size, (size_t)16);	// never zero-sized
		FString name = FStringf("VkLevelMesh.%s", BufferName(b));
		mBuffers[b] = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | src[b].usage)
			.Size(size)
			.DebugName(name.GetChars())
			.Create(dev);
		mSizes[b] = size;
		mCounts[b] = src[b].count;
		if (src[b].data && src[b].size)
		{
			transfer.AddBuffer(mBuffers[b].get(), src[b].data, src[b].size);
			anyTransfer = true;
		}
		else
		{
			cmd->fillBuffer(mBuffers[b]->buffer, 0, VK_WHOLE_SIZE, 0);
		}
	}
	if (anyTransfer)
		fb->GetCommands()->TransferDeleteList->Add(transfer.Execute(dev, cmd));
	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

	mSet = mSetPool->allocate(mSetLayout.get());
	mSet->SetDebugName("VkLevelMesh.Set");
	WriteDescriptors write;
	for (int b = 0; b < LMBIND_NUMBUFFERS; b++)
		write.AddBuffer(mSet.get(), b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mBuffers[b].get());
	write.Execute(dev);
	WriteSamplers();

	mTable.assign(data->translationTableSize, LevelMeshTextureEntry{ 0, 0 });
	mPendingFlag.assign(data->translationTableSize, 0);
	for (unsigned i = 0; i < data->textureIds.size; i++)
	{
		int id = data->textureIds.data[i];
		if (id > 0 && (unsigned)id < mTable.size() && !mPendingFlag[id])
		{
			mPendingFlag[id] = 1;
			mPending.push_back(id);
		}
	}

	mId = data->id;
	mResident = true;
	mUploadMs = I_msTimeF() - t0;
}

//==========================================================================
//
// Samplers
//
//==========================================================================

void VkLevelMesh::WriteSamplers()
{
	mSamplersDirty = false;
	if (!mSet) return;
	VkDescriptorImageInfo infos[NUMSAMPLERS];
	for (int i = 0; i < NUMSAMPLERS; i++)
		infos[i] = { fb->GetSamplerManager()->Get(i)->sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
	VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	w.dstSet = mSet->set;
	w.dstBinding = LMBIND_SAMPLERS;
	w.descriptorCount = NUMSAMPLERS;
	w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	w.pImageInfo = infos;
	vkUpdateDescriptorSets(fb->device->device, 1, &w, 0, nullptr);
}

//==========================================================================
//
// Bindless slots
//
//==========================================================================

uint32_t VkLevelMesh::AllocSlots(uint32_t count)
{
	if (count < mFreeBlocks.size() && !mFreeBlocks[count].empty())
	{
		uint32_t first = mFreeBlocks[count].back();
		mFreeBlocks[count].pop_back();
		mSlotsUsed += count;
		return first;
	}
	if (mNextSlot + count > mBindlessCapacity) return 0;
	uint32_t first = mNextSlot;
	mNextSlot += count;
	mSlotsUsed += count;
	return first;
}

void VkLevelMesh::FreeSlots(uint32_t first, uint32_t count)
{
	if (first == 0 || count == 0) return;
	if (mFreeBlocks.size() <= count) mFreeBlocks.resize(count + 1);
	mFreeBlocks[count].push_back(first);
	mSlotsUsed -= count;
}

void VkLevelMesh::WriteSlot(uint32_t slot, VkImageView view, VkImageLayout layout)
{
	mSlotWrites.push_back({ slot, view, layout });
}

void VkLevelMesh::FlushSlotWrites()
{
	if (mSlotWrites.empty() || !mTextureSet) return;
	std::vector<VkDescriptorImageInfo> infos(mSlotWrites.size());
	std::vector<VkWriteDescriptorSet> writes(mSlotWrites.size());
	for (size_t i = 0; i < mSlotWrites.size(); i++)
	{
		infos[i] = { VK_NULL_HANDLE, mSlotWrites[i].view, mSlotWrites[i].layout };
		auto &w = writes[i];
		w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		w.dstSet = mTextureSet->set;
		w.dstBinding = 0;
		w.dstArrayElement = mSlotWrites[i].slot;
		w.descriptorCount = 1;
		w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		w.pImageInfo = &infos[i];
	}
	vkUpdateDescriptorSets(fb->device->device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
	mSlotWrites.clear();
}

// Untranslated texture id -> slots of its current (animated) material.
// Indexed (palette) materials are bound with their base layer only.
LevelMeshTextureEntry VkLevelMesh::Resolve(int id)
{
	auto tex = TexMan.GetGameTexture(FSetTextureID(id), true);
	if (tex == nullptr || !tex->isValid()) return { 0, 0 };
	int scaleflags = shouldUpscale(tex, UF_Texture) ? CTF_Upscale : 0;
	auto mat = static_cast<VkMaterial *>(FMaterial::ValidateTexture(tex, scaleflags));
	if (mat == nullptr) return { 0, 0 };

	auto it = mMaterials.find(mat);
	if (it == mMaterials.end())
	{
		MaterialLayerInfo *layer;
		auto base = static_cast<VkHardwareTexture *>(mat->GetLayer(0, 0, &layer));
		uint32_t count = (layer->scaleFlags & CTF_Indexed) ? 1 : (uint32_t)mat->NumLayers();
		uint32_t first = AllocSlots(count);
		if (first == 0)
		{
			mSlotOverflows++;
			return { 0, 0 };
		}
		for (uint32_t i = 0; i < count; i++)
		{
			auto systex = i == 0 ? base : static_cast<VkHardwareTexture *>(mat->GetLayer(i, 0, &layer));
			auto image = systex->GetImage(layer->layerTexture, 0, layer->scaleFlags);
			WriteSlot(first + i, image->View->view, image->Layout);
		}
		it = mMaterials.emplace(mat, MaterialSlots{ first, count, {} }).first;
	}
	it->second.ids.push_back(id);
	return { it->second.first, it->second.count };
}

void VkLevelMesh::MaterialInvalidated(VkMaterial *mat)
{
	auto it = mMaterials.find(mat);
	if (it == mMaterials.end()) return;
	FreeSlots(it->second.first, it->second.count);
	for (int id : it->second.ids)
	{
		if ((unsigned)id >= mTable.size()) continue;
		mTable[id] = { 0, 0 };
		mTableDirtyMin = std::min(mTableDirtyMin, (uint32_t)id);
		mTableDirtyMax = std::max(mTableDirtyMax, (uint32_t)id);
		if (!mPendingFlag[id])
		{
			mPendingFlag[id] = 1;
			mPending.push_back(id);
		}
	}
	mMaterials.erase(it);
}

//==========================================================================
//
// Frame start
//
//==========================================================================

void VkLevelMesh::BeginFrame()
{
	mRingIndex = (mRingIndex + 1) % HW_MAX_PIPELINE_BUFFERS;
	mRing[mRingIndex].used = 0;
	if (!mResident) return;

	if (mSamplersDirty) WriteSamplers();

	if (!mPending.empty())
	{
		for (int id : mPending)
		{
			mPendingFlag[id] = 0;
			mTable[id] = Resolve(id);
			mResolveCalls++;
			mTableDirtyMin = std::min(mTableDirtyMin, (uint32_t)id);
			mTableDirtyMax = std::max(mTableDirtyMax, (uint32_t)id);
		}
		mPending.clear();
	}
	FlushSlotWrites();
	UploadTable();
	if (!mCopies.empty())
		FlushUploads(fb->GetCommands()->GetTransferCommands());
}

void VkLevelMesh::UploadTable()
{
	if (mTableDirtyMin > mTableDirtyMax || mTable.empty()) return;
	uint32_t first = mTableDirtyMin, count = mTableDirtyMax - mTableDirtyMin + 1;
	size_t size = count * sizeof(LevelMeshTextureEntry);
	auto alloc = AllocUpload(size);
	memcpy(alloc.data, &mTable[first], size);
	QueueCopy(alloc, mBuffers[LMBIND_TEXTURETABLE].get(), first * sizeof(LevelMeshTextureEntry), size);
	mTableDirtyMin = ~0u;
	mTableDirtyMax = 0;
}

//==========================================================================
//
// Staging ring
//
//==========================================================================

VkLevelMesh::UploadAlloc VkLevelMesh::AllocUpload(size_t size, size_t align)
{
	auto &slot = mRing[mRingIndex];
	size_t offset = (slot.used + align - 1) / align * align;
	if (!slot.buffer || offset + size > slot.buffer->size)
	{
		size_t newsize = std::max({ MIN_RING_SIZE, slot.buffer ? slot.buffer->size * 2 : 0, size });
		// Copies queued from the old buffer this frame still read it: delete it after the frame.
		if (slot.buffer) slot.buffer->Unmap();
		fb->GetCommands()->DrawDeleteList->Add(std::move(slot.buffer));
		slot.buffer = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_UNKNOWN, VMA_ALLOCATION_CREATE_MAPPED_BIT)
			.MemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
			.Size(newsize)
			.DebugName("VkLevelMesh.StagingRing")
			.Create(fb->device.get());
		slot.data = (uint8_t *)slot.buffer->Map(0, newsize);
		offset = 0;
	}
	slot.used = offset + size;
	return { slot.buffer.get(), offset, slot.data + offset };
}

void VkLevelMesh::QueueCopy(const UploadAlloc &src, VulkanBuffer *dst, size_t dstOffset, size_t size)
{
	mCopies.push_back({ src.buffer, dst, { src.offset, dstOffset, size } });
}

void VkLevelMesh::FlushUploads(VulkanCommandBuffer *cmd)
{
	if (mCopies.empty()) return;
	std::stable_sort(mCopies.begin(), mCopies.end(), [](const PendingCopy &a, const PendingCopy &b) { return a.src != b.src ? a.src < b.src : a.dst < b.dst; });
	std::vector<VkBufferCopy> regions;
	for (size_t i = 0; i < mCopies.size();)
	{
		size_t j = i;
		regions.clear();
		while (j < mCopies.size() && mCopies[j].src == mCopies[i].src && mCopies[j].dst == mCopies[i].dst)
			regions.push_back(mCopies[j++].region);
		cmd->copyBuffer(mCopies[i].src->buffer, mCopies[i].dst->buffer, (uint32_t)regions.size(), regions.data());
		i = j;
	}
	mCopies.clear();
	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
}

//==========================================================================
//
// Stats
//
//==========================================================================

void VkLevelMesh::PrintStats()
{
	if (!Supported(false))
	{
		Supported(true);
		return;
	}
	if (!mResident)
	{
		Printf("VkLevelMesh: no level resident\n");
		return;
	}
	size_t total = 0;
	for (int b = 0; b < LMBIND_NUMBUFFERS; b++)
	{
		Printf("VkLevelMesh: %-16s %9u records %9.2f MB\n", BufferName(b), mCounts[b], mSizes[b] / (1024.0 * 1024.0));
		total += mSizes[b];
	}
	size_t ring = 0;
	for (auto &slot : mRing) if (slot.buffer) ring += slot.buffer->size;
	Printf("VkLevelMesh: device-local %.2f MB, staging ring %.2f MB (%d slots), upload %.1f ms\n", total / (1024.0 * 1024.0), ring / (1024.0 * 1024.0), HW_MAX_PIPELINE_BUFFERS, mUploadMs);
	unsigned resolved = 0;
	for (auto &e : mTable) if (e.slot) resolved++;
	Printf("VkLevelMesh: %u clusters, %u triangles; bindless slots %u used / %u capacity (high water %u), %u materials, %u overflows; texture table %u entries, %u resolved, %u pending, %u resolve calls\n",
		mCounts[LMBIND_CLUSTERS], mCounts[LMBIND_INDICES] / 3, mSlotsUsed, mBindlessCapacity, mNextSlot, (unsigned)mMaterials.size(), mSlotOverflows,
		(unsigned)mTable.size(), resolved, (unsigned)mPending.size(), mResolveCalls);
}
