/*
** vk_levelmeshrenderer.cpp
**
** GPU cull + indirect draw of the levelmesh (main view)
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Frame sequence of one Draw (LMCULL_HIZ):
**
**   end the scene pass
**   upload the shading block if it changed
**   reset counters, cull phase 1 (clusters visible last frame, frustum)
**   finalize (compaction: write the draw arguments)
**   resolve the shading of phase 1's clusters
**   draw phase 1                          (resumes the scene pass)
**   end the scene pass; depth -> read-only; build the Hi-Z pyramid; depth back
**   cull phase 2 (all clusters, frustum + Hi-Z; rewrites the visibility)
**   finalize, copy the counters to the readback ring
**   resolve the shading of phase 2's clusters
**   draw phase 2                          (resumes the scene pass)
**
** LMCULL_FRUSTUM / LMCULL_NONE run one cull, resolve and draw. The render pass
** resumes with the attachments loaded; if the scene pass had not started
** yet, the first resume consumes the pending clear.
**
** The Hi-Z level 0 is the viewport of the scene depth at full resolution
** (max over the MSAA samples); later levels round down and fold the odd
** remainder into the last row / column (hiz.comp).
*/

// HEADER FILES ------------------------------------------------------------

#include <algorithm>
#include <cstring>

#include "vk_levelmeshrenderer.h"
#include "vk_levelmesh.h"
#include "vk_renderstate.h"
#include "vk_renderpass.h"
#include "vk_descriptorset.h"
#include "vulkan/shaders/vk_shader.h"
#include "vulkan/system/vk_renderdevice.h"
#include "vulkan/system/vk_commandbuffer.h"
#include "vulkan/textures/vk_renderbuffers.h"
#include <zvulkan/vulkanbuilders.h>
#include "hw_renderstate.h"
#include "c_cvars.h"
#include "printf.h"

// TYPES -------------------------------------------------------------------

// Cull phases (cull.comp LMPHASE_*)
enum
{
	LMPHASE_FIRST,
	LMPHASE_SECOND,
	LMPHASE_SINGLE,
	LMPHASE_FINALIZE,
};

// Counter buffer layout (uint32 each, cull.comp LMC_*). Everything before
// LMC_COUNT is cleared per Draw; the bucket bases after it are per level.
enum
{
	LMC_COMPACT = 0,							// [LMB_DRAWN] compaction cursor per bucket, both phases
	LMC_P1END = LMC_COMPACT + LMB_DRAWN,		// [LMB_DRAWN] end of phase 1 per bucket
	LMC_COMMANDS = LMC_P1END + LMB_DRAWN,		// [2 * LMB_DRAWN] per-cluster commands per phase and bucket
	LMC_CLUSTERS = LMC_COMMANDS + 2 * LMB_DRAWN,	// [2] clusters drawn per phase
	LMC_INDICES = LMC_CLUSTERS + 2,				// [2 * LMB_DRAWN] indices drawn per phase and bucket
	LMC_FRUSTUMCULLED = LMC_INDICES + 2 * LMB_DRAWN,
	LMC_OCCLUDED,
	LMC_COUNT,
	LMC_BASE = LMC_COUNT,						// [LMB_DRAWN] compacted index region per bucket (per level)
	LMC_TOTAL = LMC_BASE + LMB_DRAWN,
};

// Timestamp points
enum
{
	TS_START,
	TS_CULL1,
	TS_RES1,
	TS_DRAW1,
	TS_HIZ,
	TS_CULL2,
	TS_RES2,
	TS_DRAW2,
	TS_COUNT
};

static_assert(LMC_COUNT == 6 * LMB_DRAWN + 4, "VkLevelMeshRenderer::NumCounters");
static_assert(TS_COUNT <= 8, "VkLevelMeshRenderer::NumPoints");

// PUBLIC DATA DEFINITIONS -------------------------------------------------

// Debug: back-face culling of the levelmesh draws (0 = off, 1 = on).
CVAR(Int, lm_backfacecull, 1, CVAR_NOSAVE)

// CODE --------------------------------------------------------------------

VkLevelMeshRenderer::VkLevelMeshRenderer(VulkanRenderDevice *fb) : fb(fb)
{
}

VkLevelMeshRenderer::~VkLevelMeshRenderer()
{
	if (mReadback && mReadbackData) mReadback->Unmap();
	if (mVisReadback && mVisReadbackData) mVisReadback->Unmap();
}

static FString CullDefines()
{
	FString d = VkLevelMesh::ShaderDefines(0, -1);
	d << "#define LM_CULL_SET 1\n";
	d.AppendFormat("#define LMPHASE_FIRST %uu\n#define LMPHASE_SECOND %uu\n#define LMPHASE_SINGLE %uu\n#define LMPHASE_FINALIZE %uu\n",
		(unsigned)LMPHASE_FIRST, (unsigned)LMPHASE_SECOND, (unsigned)LMPHASE_SINGLE, (unsigned)LMPHASE_FINALIZE);
	d.AppendFormat("#define LMC_COMPACT %uu\n#define LMC_P1END %uu\n#define LMC_COMMANDS %uu\n#define LMC_CLUSTERS %uu\n",
		(unsigned)LMC_COMPACT, (unsigned)LMC_P1END, (unsigned)LMC_COMMANDS, (unsigned)LMC_CLUSTERS);
	d.AppendFormat("#define LMC_INDICES %uu\n#define LMC_FRUSTUMCULLED %uu\n#define LMC_OCCLUDED %uu\n#define LMC_BASE %uu\n",
		(unsigned)LMC_INDICES, (unsigned)LMC_FRUSTUMCULLED, (unsigned)LMC_OCCLUDED, (unsigned)LMC_BASE);
	return d;
}

//==========================================================================
//
// Device objects (on first draw)
//
//==========================================================================

bool VkLevelMeshRenderer::CreateObjects()
{
	if (mCreated) return !mFailed;
	mCreated = true;
	auto dev = fb->device.get();
	auto lm = fb->GetLevelMesh();
	auto shaders = fb->GetShaderManager();

	try
	{
		mCullShader = shaders->LoadComputeShader("levelmesh_cull", "shaders/levelmesh/cull.comp", CullDefines().GetChars());
		mResolveShader = shaders->LoadComputeShader("levelmesh_resolve", "shaders/levelmesh/resolve.comp", CullDefines().GetChars());
		mHiZShaders[0] = shaders->LoadComputeShader("levelmesh_hiz_depth", "shaders/levelmesh/hiz.comp", "#define HIZ_FROM_DEPTH\n");
		mHiZShaders[1] = shaders->LoadComputeShader("levelmesh_hiz_depthms", "shaders/levelmesh/hiz.comp", "#define HIZ_FROM_DEPTH_MS\n");
		mHiZShaders[2] = shaders->LoadComputeShader("levelmesh_hiz_level", "shaders/levelmesh/hiz.comp", "");
		for (int pass = NORMAL_PASS; pass <= GBUFFER_PASS; pass++)
		{
			shaders->GetLevelMesh(false, (EPassType)pass, LMP_DEFAULT);
			shaders->GetLevelMesh(true, (EPassType)pass, LMP_DEFAULT);
		}
	}
	catch (const CVulkanError &e)
	{
		Printf("Levelmesh: shader compile failed, using the classic path: %s\n", e.what());
		mFailed = true;
		return false;
	}

	mCullSetLayout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.DebugName("VkLevelMeshRenderer.CullSetLayout")
		.Create(dev);
	mCullLayout = PipelineLayoutBuilder()
		.AddSetLayout(lm->GetSetLayout())
		.AddSetLayout(mCullSetLayout.get())
		.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullConstants))
		.DebugName("VkLevelMeshRenderer.CullLayout")
		.Create(dev);
	mCullPipeline = ComputePipelineBuilder()
		.Layout(mCullLayout.get())
		.ComputeShader(mCullShader.get())
		.DebugName("VkLevelMeshRenderer.CullPipeline")
		.Create(dev);
	mResolveLayout = PipelineLayoutBuilder()
		.AddSetLayout(lm->GetSetLayout())
		.AddSetLayout(mCullSetLayout.get())
		.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ResolveConstants))
		.DebugName("VkLevelMeshRenderer.ResolveLayout")
		.Create(dev);
	mResolvePipeline = ComputePipelineBuilder()
		.Layout(mResolveLayout.get())
		.ComputeShader(mResolveShader.get())
		.DebugName("VkLevelMeshRenderer.ResolvePipeline")
		.Create(dev);

	mHiZSetLayout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.DebugName("VkLevelMeshRenderer.HiZSetLayout")
		.Create(dev);
	mHiZLayout = PipelineLayoutBuilder()
		.AddSetLayout(mHiZSetLayout.get())
		.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HiZConstants))
		.DebugName("VkLevelMeshRenderer.HiZLayout")
		.Create(dev);
	for (int i = 0; i < 3; i++)
	{
		mHiZPipelines[i] = ComputePipelineBuilder()
			.Layout(mHiZLayout.get())
			.ComputeShader(mHiZShaders[i].get())
			.DebugName("VkLevelMeshRenderer.HiZPipeline")
			.Create(dev);
	}

	// Sets are replaced on level / size changes (old ones go to the delete
	// list), so the pool holds a few generations.
	mPool = DescriptorPoolBuilder()
		.MaxSets(512)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7 * (MaxPortalViews + 4))
		.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256)
		.DebugName("VkLevelMeshRenderer.Pool")
		.Create(dev);

	mPointSampler = SamplerBuilder()
		.MinFilter(VK_FILTER_NEAREST)
		.MagFilter(VK_FILTER_NEAREST)
		.MipmapMode(VK_SAMPLER_MIPMAP_MODE_NEAREST)
		.AddressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
		.MaxLod(16.0f)
		.DebugName("VkLevelMeshRenderer.PointSampler")
		.Create(dev);

	if (fb->device->GraphicsTimeQueries)
	{
		mQueries = QueryPoolBuilder()
			.QueryType(VK_QUERY_TYPE_TIMESTAMP, NumSlots * NumPoints)
			.Create(dev);
		mPortalQueries = QueryPoolBuilder()
			.QueryType(VK_QUERY_TYPE_TIMESTAMP, NumSlots * 2 * MaxPortalViews)
			.Create(dev);
	}
	mReadback = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU)
		.Size(NumSlots * NumCounters * sizeof(uint32_t))
		.DebugName("VkLevelMeshRenderer.Readback")
		.Create(dev);
	mReadbackData = (uint32_t *)mReadback->Map(0, NumSlots * NumCounters * sizeof(uint32_t));
	memset(mReadbackData, 0, NumSlots * NumCounters * sizeof(uint32_t));
	return true;
}

//==========================================================================
//
// Per-level buffers
//
//==========================================================================

bool VkLevelMeshRenderer::PrepareLevel(VkLevelMesh *lm, VulkanCommandBuffer *cmd)
{
	if (mLevelId == lm->GetId() && mMain.cullSet) return true;

	auto dev = fb->device.get();
	auto deleteList = fb->GetCommands()->DrawDeleteList.get();
	auto release = [&](ViewBuffers &v)
	{
		deleteList->Add(std::move(v.visibility));
		deleteList->Add(std::move(v.counters));
		deleteList->Add(std::move(v.drawArgs));
		deleteList->Add(std::move(v.compact));
		deleteList->Add(std::move(v.drawn));
		deleteList->Add(std::move(v.visBits));
		deleteList->Add(std::move(v.cullSet));
	};
	release(mMain);
	for (auto &v : mPortals) release(*v);
	mPortals.clear();
	mShading.reset();
	if (mVisReadback && mVisReadbackData) mVisReadback->Unmap();
	mVisReadbackData = nullptr;
	deleteList->Add(std::move(mVisReadback));
	mVisWords = 0;
	mReadSlot = -1;

	// The material programs this level draws (warp variants on demand).
	try
	{
		auto shaders = fb->GetShaderManager();
		for (int b = 0; b < (int)LMB_DRAWN; b++)
		{
			if (lm->GetBucketIndexCount(b) == 0) continue;
			for (int pass = NORMAL_PASS; pass <= GBUFFER_PASS; pass++)
				shaders->GetLevelMesh((b & 1) != 0, (EPassType)pass, b >> 1);
		}
	}
	catch (const CVulkanError &e)
	{
		Printf("Levelmesh: material shader compile failed, using the classic path: %s\n", e.what());
		mFailed = true;
		return false;
	}

	mLevelId = lm->GetId();
	mNumClusters = lm->GetClusterCount();
	size_t compactCount = 0;
	for (int b = 0; b < (int)LMB_DRAWN; b++)
	{
		mBucketBase[b] = (uint32_t)compactCount;
		mBucketCount[b] = lm->GetBucketIndexCount(b);
		compactCount += mBucketCount[b];
	}
	compactCount = std::max<size_t>(compactCount, 4);

	mMain.visibility = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(std::max(mNumClusters, 1u) * sizeof(uint32_t))
		.DebugName("VkLevelMeshRenderer.Visibility")
		.Create(dev);
	mMain.counters = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
		.Size(LMC_TOTAL * sizeof(uint32_t))
		.DebugName("VkLevelMeshRenderer.Counters")
		.Create(dev);
	// 2 * LMB_DRAWN compaction draws, then 2 * LMB_DRAWN regions of per-cluster commands.
	mMain.drawArgs = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
		.Size((2 * LMB_DRAWN + 2 * LMB_DRAWN * (size_t)std::max(mNumClusters, 1u)) * sizeof(VkDrawIndexedIndirectCommand))
		.DebugName("VkLevelMeshRenderer.DrawArgs")
		.Create(dev);
	mMain.drawn = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		.Size(std::max(mNumClusters, 1u) * sizeof(uint32_t))
		.DebugName("VkLevelMeshRenderer.Drawn")
		.Create(dev);
	mVisWords = std::max((mNumClusters + 31) / 32, 1u);
	mMain.visBits = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
		.Size(mVisWords * sizeof(uint32_t))
		.DebugName("VkLevelMeshRenderer.VisBits")
		.Create(dev);
	size_t visReadbackSize = (size_t)NumSlots * (1 + MaxPortalViews) * mVisWords * sizeof(uint32_t);
	mVisReadback = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU)
		.Size(visReadbackSize)
		.DebugName("VkLevelMeshRenderer.VisReadback")
		.Create(dev);
	mVisReadbackData = (uint32_t *)mVisReadback->Map(0, visReadbackSize);
	for (auto &s : mSlots)
	{
		s.levelId = 0;
		s.viewCount = 0;
	}
	mMain.compact = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
		.Size(compactCount * sizeof(uint32_t))
		.DebugName("VkLevelMeshRenderer.CompactIndices")
		.Create(dev);

	// Nothing was visible "last frame": the first frame draws everything in phase 2.
	cmd->fillBuffer(mMain.visibility->buffer, 0, VK_WHOLE_SIZE, 0);
	cmd->updateBuffer(mMain.counters->buffer, LMC_BASE * sizeof(uint32_t), sizeof(mBucketBase), mBucketBase);
	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	UpdateCullSet(mMain);
	return true;
}

void VkLevelMeshRenderer::UpdateCullSet(ViewBuffers &v)
{
	if (!v.visibility || !mHiZView || !mMain.compact) return;
	fb->GetCommands()->DrawDeleteList->Add(std::move(v.cullSet));
	v.cullSet = mPool->allocate(mCullSetLayout.get());
	v.cullSet->SetDebugName("VkLevelMeshRenderer.CullSet");
	// A portal view's set binds the main view's compacted indices (unused:
	// per-cluster commands) and Hi-Z (unused: one frustum phase).
	WriteDescriptors()
		.AddBuffer(v.cullSet.get(), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, v.visibility.get())
		.AddBuffer(v.cullSet.get(), 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, v.counters.get())
		.AddBuffer(v.cullSet.get(), 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, v.drawArgs.get())
		.AddBuffer(v.cullSet.get(), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (v.compact ? v.compact : mMain.compact).get())
		.AddCombinedImageSampler(v.cullSet.get(), 4, mHiZView.get(), mPointSampler.get(), VK_IMAGE_LAYOUT_GENERAL)
		.AddBuffer(v.cullSet.get(), 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, v.drawn.get())
		.AddBuffer(v.cullSet.get(), 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, v.visBits.get())
		.Execute(fb->device.get());
}

// The buffers of the next portal view of this frame (null past MaxPortalViews);
// 'index': the view's index in this frame.
VkLevelMeshRenderer::ViewBuffers *VkLevelMeshRenderer::PortalBuffers(VulkanCommandBuffer *cmd, unsigned &index)
{
	if (mPortalViews >= MaxPortalViews) return nullptr;
	index = mPortalViews++;
	if (index < mPortals.size()) return mPortals[index].get();
	auto dev = fb->device.get();
	auto v = std::make_unique<ViewBuffers>();
	size_t clusters = std::max(mNumClusters, 1u);
	v->visibility = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
		.Size(clusters * sizeof(uint32_t))
		.DebugName("VkLevelMeshRenderer.PortalVisibility")
		.Create(dev);
	v->counters = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
		.Size(LMC_TOTAL * sizeof(uint32_t))
		.DebugName("VkLevelMeshRenderer.PortalCounters")
		.Create(dev);
	// One phase: the per-cluster command regions of phase slot 0 only.
	v->drawArgs = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
		.Size((2 * LMB_DRAWN + LMB_DRAWN * clusters) * sizeof(VkDrawIndexedIndirectCommand))
		.DebugName("VkLevelMeshRenderer.PortalDrawArgs")
		.Create(dev);
	v->drawn = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		.Size(clusters * sizeof(uint32_t))
		.DebugName("VkLevelMeshRenderer.PortalDrawn")
		.Create(dev);
	v->visBits = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
		.Size(mVisWords * sizeof(uint32_t))
		.DebugName("VkLevelMeshRenderer.PortalVisBits")
		.Create(dev);
	UpdateCullSet(*v);
	mPortals.push_back(std::move(v));
	return mPortals.back().get();
}

//==========================================================================
//
// Hi-Z resources: level 0 = the viewport size of the scene depth
//
//==========================================================================

bool VkLevelMeshRenderer::PrepareHiZ(int width, int height, VkSampleCountFlagBits samples)
{
	auto buffers = fb->GetBuffers();
	VulkanImageView *depthView = buffers->SceneDepthStencil.DepthOnlyView.get();
	if (!depthView || width <= 0 || height <= 0) return false;
	if (mHiZ && mDepthView == depthView && mHiZWidth == width && mHiZHeight == height && mHiZSamples == samples) return true;

	auto dev = fb->device.get();
	auto deleteList = fb->GetCommands()->DrawDeleteList.get();
	deleteList->Add(std::move(mHiZView));
	for (auto &v : mHiZLevelViews) deleteList->Add(std::move(v));
	for (auto &s : mHiZSets) deleteList->Add(std::move(s));
	deleteList->Add(std::move(mHiZ));
	mHiZLevelViews.clear();
	mHiZSets.clear();

	int levels = 1;
	while ((width >> levels) > 0 || (height >> levels) > 0)
	{
		if ((width >> levels) == 0 || (height >> levels) == 0) break;
		levels++;
	}
	mDepthView = depthView;
	mHiZWidth = width;
	mHiZHeight = height;
	mHiZLevels = levels;
	mHiZSamples = samples;

	mHiZ = ImageBuilder()
		.Size(width, height, levels)
		.Format(VK_FORMAT_R32_SFLOAT)
		.Usage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName("VkLevelMeshRenderer.HiZ")
		.Create(dev);
	mHiZFresh = true;
	mHiZView = ImageViewBuilder()
		.Image(mHiZ.get(), VK_FORMAT_R32_SFLOAT)
		.DebugName("VkLevelMeshRenderer.HiZView")
		.Create(dev);
	for (int i = 0; i < levels; i++)
	{
		mHiZLevelViews.push_back(ImageViewBuilder()
			.Image(mHiZ.get(), VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1, 1)
			.DebugName("VkLevelMeshRenderer.HiZLevelView")
			.Create(dev));
	}
	WriteDescriptors write;
	for (int i = 0; i < levels; i++)
	{
		auto set = mPool->allocate(mHiZSetLayout.get());
		set->SetDebugName("VkLevelMeshRenderer.HiZSet");
		if (i == 0)
			write.AddCombinedImageSampler(set.get(), 0, depthView, mPointSampler.get(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
		else
			write.AddCombinedImageSampler(set.get(), 0, mHiZLevelViews[i - 1].get(), mPointSampler.get(), VK_IMAGE_LAYOUT_GENERAL);
		write.AddStorageImage(set.get(), 1, mHiZLevelViews[i].get(), VK_IMAGE_LAYOUT_GENERAL);
		mHiZSets.push_back(std::move(set));
	}
	write.Execute(dev);
	UpdateCullSet(mMain);
	for (auto &v : mPortals) UpdateCullSet(*v);
	return true;
}

void VkLevelMeshRenderer::BuildHiZ(VulkanCommandBuffer *cmd)
{
	auto buffers = fb->GetBuffers();
	auto depthImage = buffers->SceneDepthStencil.Image.get();

	// Depth: attachment -> sampled. Hi-Z: the previous cull reads -> writes.
	PipelineBarrier barrier;
	barrier.AddImage(depthImage, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
	barrier.AddImage(mHiZ.get(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
		VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, mHiZLevels);
	barrier.Execute(cmd, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	int w = mHiZWidth, h = mHiZHeight;
	for (int i = 0; i < mHiZLevels; i++)
	{
		HiZConstants pc = {};
		int pipe = i > 0 ? 2 : (mHiZSamples != VK_SAMPLE_COUNT_1_BIT ? 1 : 0);
		int dw = i == 0 ? w : std::max(w >> 1, 1);
		int dh = i == 0 ? h : std::max(h >> 1, 1);
		pc.srcOffset[0] = i == 0 ? mTarget.x : 0;
		pc.srcOffset[1] = i == 0 ? mTarget.y : 0;
		pc.srcSize[0] = w;
		pc.srcSize[1] = h;
		pc.dstSize[0] = dw;
		pc.dstSize[1] = dh;
		pc.samples = (int)mHiZSamples;
		cmd->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, mHiZPipelines[pipe].get());
		cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, mHiZLayout.get(), 0, mHiZSets[i].get());
		cmd->pushConstants(mHiZLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HiZConstants), &pc);
		cmd->dispatch((dw + 7) / 8, (dh + 7) / 8, 1);
		PipelineBarrier()
			.AddMemory(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
			.Execute(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		w = dw;
		h = dh;
	}

	// Depth back to an attachment for the rest of the scene.
	PipelineBarrier()
		.AddImage(depthImage, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
}

//==========================================================================
//
// Draw pipelines
//
//==========================================================================

VulkanPipeline *VkLevelMeshRenderer::GetDrawPipeline(bool masked, int program, const VkRenderState::ExternalTarget &t)
{
	bool cull = lm_backfacecull != 0;
	uint64_t bits = (uint64_t)masked | ((uint64_t)t.drawBuffers << 1) | ((uint64_t)t.samples << 4) | ((uint64_t)t.depthTest << 12) |
		((uint64_t)t.depthWrite << 13) | ((uint64_t)t.depthClamp << 14) | ((uint64_t)t.stencilTest << 15) | ((uint64_t)t.depthFunc << 16) |
		((uint64_t)t.stencilOp << 20) | ((uint64_t)t.colorMask << 24) | ((uint64_t)cull << 28) | ((uint64_t)mMirrored << 29) | ((uint64_t)program << 32);
	PipelineKey key = { t.renderPass, bits };
	auto &pipeline = mPipelines[key];
	if (pipeline) return pipeline.get();

	EPassType passType = t.drawBuffers > 1 ? GBUFFER_PASS : NORMAL_PASS;
	auto shaders = fb->GetShaderManager();
	auto shaderProgram = shaders->GetLevelMesh(masked, passType, program);

	static const VkStencilOp op2vk[] = { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_DECREMENT_AND_CLAMP };
	static const VkCompareOp depthfunc2vk[] = { VK_COMPARE_OP_LESS, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_ALWAYS };

	GraphicsPipelineBuilder builder;
	builder.Cache(fb->GetRenderPassManager()->GetCache());
	builder.AddVertexShader(shaders->GetLevelMeshVert());
	builder.AddFragmentShader(shaderProgram->frag.get());
	builder.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
	builder.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR);
	builder.AddDynamicState(VK_DYNAMIC_STATE_DEPTH_BIAS);
	builder.AddDynamicState(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
	builder.Viewport(0.0f, 0.0f, 320.0f, 200.0f);
	builder.Scissor(0, 0, 320, 200);
	builder.Topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	builder.DepthStencilEnable(t.depthTest, t.depthWrite, t.stencilTest);
	builder.DepthFunc(depthfunc2vk[t.depthFunc]);
	if (fb->device->EnabledFeatures.Features.depthClamp)
		builder.DepthClampEnable(t.depthClamp);
	builder.DepthBias(false, 0.0f, 0.0f, 0.0f);
	// The bake keeps the classic winding (floors as the flat triangulation,
	// ceilings reversed, walls as the classic fan): visible faces are
	// counter-clockwise on screen (verified against lm_backfacecull 0).
	// A mirrored view (portalState.isMirrored()) flips the screen winding.
	builder.Cull(cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE, mMirrored ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
	builder.Stencil(VK_STENCIL_OP_KEEP, op2vk[t.stencilOp], VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 0xffffffff, 0xffffffff, 0);
	ColorBlendAttachmentBuilder blend;
	blend.ColorWriteMask((VkColorComponentFlags)t.colorMask);
	for (int i = 0; i < t.drawBuffers; i++)
		builder.AddColorBlendAttachment(blend.Create());
	builder.RasterizationSamples(t.samples);
	builder.Layout(fb->GetLevelMesh()->GetPipelineLayout());
	builder.RenderPass(t.renderPass);
	builder.DebugName(masked ? "VkLevelMeshRenderer.MaskedPipeline" : "VkLevelMeshRenderer.OpaquePipeline");
	pipeline = builder.Create(fb->device.get());
	return pipeline.get();
}

//==========================================================================
//
// Cull, draw
//
//==========================================================================

void VkLevelMeshRenderer::Cull(VulkanCommandBuffer *cmd, CullConstants &pc, uint32_t phase)
{
	pc.phase = phase;
	cmd->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, mCullPipeline.get());
	cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, mCullLayout.get(), 0, fb->GetLevelMesh()->GetSet());
	cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, mCullLayout.get(), 1, mView->cullSet.get());
	cmd->pushConstants(mCullLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullConstants), &pc);
	uint32_t groups = std::max(mNumClusters, 1u);
	uint32_t gx = std::min(groups, 65535u);
	cmd->dispatch(gx, (groups + gx - 1) / gx, 1);
	PipelineBarrier()
		.AddMemory(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	if (pc.drawMode == LMDRAW_COMPACT)
	{
		pc.phase = LMPHASE_FINALIZE;
		pc.finalizePhase = phase;
		cmd->pushConstants(mCullLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullConstants), &pc);
		cmd->dispatch(1, 1, 1);
	}
	PipelineBarrier()
		.AddMemory(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
}

void VkLevelMeshRenderer::Resolve(VulkanCommandBuffer *cmd)
{
	// The previous draw's vertex stage reads the records this rewrites.
	PipelineBarrier()
		.AddMemory(VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	ResolveConstants pc = {};
	pc.numClusters = mNumClusters;
	cmd->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, mResolvePipeline.get());
	cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, mResolveLayout.get(), 0, fb->GetLevelMesh()->GetSet());
	cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, mResolveLayout.get(), 1, mView->cullSet.get());
	cmd->pushConstants(mResolveLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ResolveConstants), &pc);
	uint32_t groups = std::max(mNumClusters, 1u);
	uint32_t gx = std::min(groups, 65535u);
	cmd->dispatch(gx, (groups + gx - 1) / gx, 1);
	PipelineBarrier()
		.AddMemory(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
}

void VkLevelMeshRenderer::UploadShading(VkLevelMesh *lm, VulkanCommandBuffer *cmd, const LevelMeshShading &shading)
{
	VulkanBuffer *buffer = lm->GetBuffer(LMBIND_SHADING);
	if (mShading && mShadingBuffer == buffer && memcmp(mShading.get(), &shading, sizeof(shading)) == 0) return;
	if (!mShading) mShading = std::make_unique<LevelMeshShading>();
	*mShading = shading;
	mShadingBuffer = buffer;
	PipelineBarrier()
		.AddMemory(VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	static_assert(sizeof(LevelMeshShading) <= 65536 && sizeof(LevelMeshShading) % 4 == 0, "vkCmdUpdateBuffer limits");
	cmd->updateBuffer(buffer->buffer, 0, sizeof(LevelMeshShading), &shading);
	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void VkLevelMeshRenderer::DrawPhase(VkLevelMesh *lm, const LevelMeshView &view, int phaseSlot)
{
	if (view.firstSky)
	{
		// HWPortal::RemoveStencil without a stencil: the portal's geometry
		// into depth (LEqual) and alpha, ahead of the scene.
		VkRenderState::ExternalTarget sky = mTarget;
		sky.depthTest = true;
		sky.depthWrite = true;
		sky.depthFunc = 1;	// DF_LEqual
		sky.stencilOp = 0;
		sky.colorMask = 8;	// alpha
		DrawSkyBucket(lm, phaseSlot, view.firstSky, sky);
	}
	auto state = fb->GetRenderState();
	auto layout = lm->GetPipelineLayout();
	PushConstants pc;
	auto cmd = state->BeginExternalDraw(layout, pc);
	pc.uTextureMode &= 0xffff;	// the material layer flags are per surface (lm_frag.glsl)
	pc.uLightIndex = -1;
	pc.uLevelMeshFlags = (int)view.flags;
	// VkRenderState::ApplyStreamData: the material timer at speed 1 (lm_frag.glsl scales it per surface).
	pc.uLevelMeshTime = static_cast<float>((double)(screen->FrameTime - state->firstFrame) / 1000.);
	if (view.shading == nullptr) pc.uLevelMeshFlags |= LMVF_FULLBRIGHT;	// nothing resolved
	cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, VkLevelMesh::LM_SET_INDEX, lm->GetSet());
	cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, VkLevelMesh::LM_TEXTURE_SET_INDEX, lm->GetTextureSet());
	bool compact = view.drawMode == LMDRAW_COMPACT;
	cmd->bindIndexBuffer(compact ? mView->compact->buffer : lm->GetIndexBuffer()->buffer, 0, VK_INDEX_TYPE_UINT32);

	const uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);
	for (int b = 0; b < (int)LMB_DRAWN; b++)
	{
		if (mBucketCount[b] == 0 || (b >> 1) == (int)LMP_SKY) continue;
		bool masked = (b & 1) != 0;
		cmd->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, GetDrawPipeline(masked, b >> 1, mTarget));
		// As HWDrawInfo::RenderScene: AlphaFunc(Alpha_GEqual, gl_mask_threshold).
		pc.uAlphaThreshold = masked ? view.maskThreshold - 0.001f : -0.001f;
		cmd->pushConstants(layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
		uint32_t slot = phaseSlot * LMB_DRAWN + b;
		if (compact)
		{
			cmd->drawIndexedIndirect(mView->drawArgs->buffer, slot * stride, 1, stride);
		}
		else
		{
			VkDeviceSize offset = (2 * LMB_DRAWN + (VkDeviceSize)slot * mNumClusters) * stride;
			vkCmdDrawIndexedIndirectCountKHR(cmd->buffer, mView->drawArgs->buffer, offset, mView->counters->buffer, (LMC_COMMANDS + slot) * sizeof(uint32_t),
				std::max(mNumClusters, 1u), stride);
		}
	}
	state->EndExternalDraw();
}

//==========================================================================
//
// Sky (ticket 14)
//
// The sky surfaces are culled with the rest but only drawn for a sky
// configuration: by its HWSkyPortal's stencil passes (DrawSky, with the
// render state the portal set up), or, for the configuration whose portal
// went before the scene without a stencil, ahead of the opaque buckets of
// each phase in the state of HWPortal::RemoveStencil's depth restore
// (depth LEqual, written; color mask alpha only).
//
//==========================================================================

void VkLevelMeshRenderer::UploadSkySources(VkLevelMesh *lm, VulkanCommandBuffer *cmd, const LevelMeshView &view)
{
	VulkanBuffer *buffer = lm->GetBuffer(LMBIND_SKYSOURCES);
	unsigned n = std::min<unsigned>(view.numSkySources, lm->GetSkySourceCount());
	if (n == 0) return;
	if (mSkySourceBuffer == buffer && mSkySources.size() == n && memcmp(mSkySources.data(), view.skySources, n * sizeof(uint32_t)) == 0) return;
	mSkySources.assign(view.skySources, view.skySources + n);
	mSkySourceBuffer = buffer;
	PipelineBarrier()
		.AddMemory(VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	const size_t chunk = 65536 / sizeof(uint32_t);	// vkCmdUpdateBuffer limit
	for (size_t i = 0; i < n; i += chunk)
	{
		size_t count = std::min<size_t>(chunk, n - i);
		cmd->updateBuffer(buffer->buffer, i * sizeof(uint32_t), count * sizeof(uint32_t), view.skySources + i);
	}
	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
}

void VkLevelMeshRenderer::DrawSkyBucket(VkLevelMesh *lm, int phaseSlot, uint32_t config, const VkRenderState::ExternalTarget &target)
{
	const int b = (int)LM_Bucket(LMP_SKY, false);
	if (mBucketCount[b] == 0 || config == 0) return;
	auto state = fb->GetRenderState();
	auto layout = lm->GetPipelineLayout();
	PushConstants pc;
	auto cmd = state->BeginExternalDraw(layout, pc);
	pc.uTextureMode &= 0xffff;
	pc.uLightIndex = -1;
	pc.uLevelMeshFlags = (int)(LMVF_FULLBRIGHT | (config << LMVF_SKYSHIFT));
	pc.uAlphaThreshold = -0.001f;
	cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, VkLevelMesh::LM_SET_INDEX, lm->GetSet());
	cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, VkLevelMesh::LM_TEXTURE_SET_INDEX, lm->GetTextureSet());
	bool compact = mDrawDrawMode == LMDRAW_COMPACT;
	cmd->bindIndexBuffer(compact ? mView->compact->buffer : lm->GetIndexBuffer()->buffer, 0, VK_INDEX_TYPE_UINT32);
	cmd->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, GetDrawPipeline(false, LMP_SKY, target));
	cmd->pushConstants(layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
	const uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);
	uint32_t slot = phaseSlot * LMB_DRAWN + b;
	if (compact)
	{
		cmd->drawIndexedIndirect(mView->drawArgs->buffer, slot * stride, 1, stride);
	}
	else
	{
		VkDeviceSize offset = (2 * LMB_DRAWN + (VkDeviceSize)slot * mNumClusters) * stride;
		vkCmdDrawIndexedIndirectCountKHR(cmd->buffer, mView->drawArgs->buffer, offset, mView->counters->buffer, (LMC_COMMANDS + slot) * sizeof(uint32_t),
			std::max(mNumClusters, 1u), stride);
	}
	state->EndExternalDraw();
}

bool VkLevelMeshRenderer::DrawSky(uint32_t config)
{
	auto lm = fb->GetLevelMesh();
	if (!lm || lm->GetId() == 0 || lm->GetId() != mLevelId || mDrawFrame != mFrame || !mMain.compact) return false;
	auto ext = fb->GetRenderState()->GetExternalTarget();
	if (!ext.depthStencil || ext.depthStencilView != fb->GetBuffers()->SceneDepthStencil.View.get()) return false;
	// The main view's culls (portal views never draw sky surfaces).
	mView = &mMain;
	mMirrored = false;
	DrawSkyBucket(lm, 0, config, ext);
	if (mDrawCullMode == LMCULL_HIZ) DrawSkyBucket(lm, 1, config, ext);
	return true;
}

void VkLevelMeshRenderer::Timestamp(VulkanCommandBuffer *cmd, int point)
{
	if (!mQueries) return;
	cmd->writeTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mQueries.get(), mSlot * NumPoints + point);
	mSlots[mSlot].points |= 1u << point;
}

//==========================================================================
//
// Portal views (ticket 15): drawn inside the portal's stencil after the
// main view of the frame, frustum-culled in one phase into buffers of their
// own, with per-cluster commands. The main view's Hi-Z, visibility and draw
// arguments (DrawSky) stay untouched.
//
//==========================================================================

bool VkLevelMeshRenderer::DrawPortalView(const LevelMeshView &view)
{
	auto lm = fb->GetLevelMesh();
	if (!lm || lm->GetId() == 0 || lm->GetClusterCount() == 0) return false;
	if (mPortalViews >= MaxPortalViews) return false;
	auto state = fb->GetRenderState();
	auto ext = state->GetExternalTarget();
	if (!ext.depthStencil || ext.depthStencilView != fb->GetBuffers()->SceneDepthStencil.View.get()) return false;
	mTarget = ext;
	if (!BeginFrameSetup(lm, ext)) return false;

	state->EndRenderPass();
	auto cmd = fb->GetCommands()->GetDrawCommands();
	unsigned portalIndex = 0;
	ViewBuffers *v = PortalBuffers(cmd, portalIndex);
	if (!v) return false;
	mView = v;
	mMirrored = view.mirrored;

	bool resolve = view.shading != nullptr && !(view.flags & LMVF_FULLBRIGHT);
	if (resolve) UploadShading(lm, cmd, *view.shading);
	// Timed in the main view's slot of this frame.
	auto &slot = mSlots[mSlot];
	uint32_t query = mSlot * 2 * MaxPortalViews + 2 * slot.portalViews;
	bool timed = mPortalQueries && slot.frame == mFrame && slot.portalViews < MaxPortalViews;
	if (timed) cmd->writeTimestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mPortalQueries.get(), query);

	PipelineBarrier()
		.AddMemory(VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	cmd->fillBuffer(v->counters->buffer, 0, LMC_COUNT * sizeof(uint32_t), 0);
	cmd->fillBuffer(v->visBits->buffer, 0, VK_WHOLE_SIZE, 0);
	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	LevelMeshView pv = view;
	pv.cullMode = view.cullMode == LMCULL_NONE ? LMCULL_NONE : LMCULL_FRUSTUM;
	pv.drawMode = LMDRAW_CLUSTERS;
	pv.firstSky = 0;
	CullConstants pc = {};
	memcpy(pc.viewProj, view.viewProj, sizeof(pc.viewProj));
	if (view.cullRect[0] < view.cullRect[2] && view.cullRect[1] < view.cullRect[3])
	{
		// Clip space scaled so the window's rectangle becomes [-1, 1]: x' =
		// sx x + tx w (column-major, rows 0 / 1 combined with row 3). The cull
		// tests x / y against w only, so this narrows its frustum.
		float sx = 2.f / (view.cullRect[2] - view.cullRect[0]), tx = -(view.cullRect[2] + view.cullRect[0]) / (view.cullRect[2] - view.cullRect[0]);
		float sy = 2.f / (view.cullRect[3] - view.cullRect[1]), ty = -(view.cullRect[3] + view.cullRect[1]) / (view.cullRect[3] - view.cullRect[1]);
		for (int c = 0; c < 4; c++)
		{
			float *col = pc.viewProj + c * 4;
			col[0] = sx * col[0] + tx * col[3];
			col[1] = sy * col[1] + ty * col[3];
		}
	}
	pc.viewport[0] = (float)ext.x;
	pc.viewport[1] = (float)ext.y;
	pc.viewport[2] = (float)ext.width;
	pc.viewport[3] = (float)ext.height;
	pc.cullMode = (uint32_t)pv.cullMode;
	pc.drawMode = (uint32_t)pv.drawMode;
	pc.numClusters = mNumClusters;
	pc.hizLevels = (uint32_t)mHiZLevels;
	Cull(cmd, pc, LMPHASE_SINGLE);
	CopyVisibility(cmd, 1 + portalIndex, view.viewKey);
	if (resolve) Resolve(cmd);
	DrawPhase(lm, pv, 0);
	if (timed)
	{
		cmd->writeTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mPortalQueries.get(), query + 1);
		slot.portalViews++;
	}

	mView = &mMain;
	mMirrored = false;
	return true;
}

//==========================================================================
//
// Once per frame, by the first levelmesh view (the main view, or a portal
// drawn before the main scene: RenderFirstSkyPortal): harvest completed
// frames, the scene-sized Hi-Z, the level's per-view resources, and this
// frame's timing slot.
//
//==========================================================================

bool VkLevelMeshRenderer::BeginFrameSetup(VkLevelMesh *lm, const VkRenderState::ExternalTarget &ext)
{
	if (mSetupFrame == mFrame && mLevelId == lm->GetId() && mMain.cullSet) return true;
	if (!CreateObjects()) return false;
	Harvest();

	auto state = fb->GetRenderState();
	state->EndRenderPass();
	auto cmd = fb->GetCommands()->GetDrawCommands();
	if (!PrepareHiZ(ext.width, ext.height, ext.samples)) return false;
	if (!PrepareLevel(lm, cmd)) return false;
	if (mHiZFresh)
	{
		// Every cull phase binds the pyramid (only the second one reads it),
		// so a new image enters GENERAL before the first dispatch.
		PipelineBarrier()
			.AddImage(mHiZ.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, mHiZLevels)
			.Execute(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		mHiZFresh = false;
	}

	mSlot = (mSlot + 1) % NumSlots;
	auto &slot = mSlots[mSlot];
	slot.pending = true;
	slot.serial = ++mSerial;
	slot.cullMode = -1;
	slot.drawMode = -1;
	slot.points = 0;
	slot.frame = mFrame;
	if (mQueries) cmd->resetQueryPool(mQueries.get(), mSlot * NumPoints, NumPoints);
	if (mPortalQueries) cmd->resetQueryPool(mPortalQueries.get(), mSlot * 2 * MaxPortalViews, 2 * MaxPortalViews);
	slot.portalViews = 0;
	slot.levelId = lm->GetId();
	slot.viewCount = 0;
	mSetupFrame = mFrame;
	return true;
}

bool VkLevelMeshRenderer::Draw(const LevelMeshView &view)
{
	if (view.portal) return DrawPortalView(view);
	mView = &mMain;
	mMirrored = false;
	auto lm = fb->GetLevelMesh();
	if (!lm || lm->GetId() == 0 || lm->GetClusterCount() == 0) return false;
	auto state = fb->GetRenderState();
	auto ext = state->GetExternalTarget();
	// The main scene only: the Hi-Z and the visibility are per view.
	if (!ext.depthStencil || ext.depthStencilView != fb->GetBuffers()->SceneDepthStencil.View.get()) return false;
	mTarget = ext;
	if (!BeginFrameSetup(lm, ext)) return false;

	// Compute runs outside the render pass.
	state->EndRenderPass();
	auto cmd = fb->GetCommands()->GetDrawCommands();
	auto &slot = mSlots[mSlot];
	slot.cullMode = view.cullMode;
	slot.drawMode = view.drawMode;
	Timestamp(cmd, TS_START);

	bool resolve = view.shading != nullptr && !(view.flags & LMVF_FULLBRIGHT);
	if (resolve) UploadShading(lm, cmd, *view.shading);
	UploadSkySources(lm, cmd, view);
	mDrawFrame = mFrame;
	mDrawCullMode = view.cullMode;
	mDrawDrawMode = view.drawMode;

	// The previous draws' indirect / index reads, and the previous resolve's
	// reads of drawn[], must finish before they are rewritten.
	PipelineBarrier()
		.AddMemory(VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	cmd->fillBuffer(mView->counters->buffer, 0, LMC_COUNT * sizeof(uint32_t), 0);
	cmd->fillBuffer(mView->visBits->buffer, 0, VK_WHOLE_SIZE, 0);
	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	CullConstants pc = {};
	memcpy(pc.viewProj, view.viewProj, sizeof(pc.viewProj));
	pc.viewport[0] = (float)ext.x;
	pc.viewport[1] = (float)ext.y;
	pc.viewport[2] = (float)ext.width;
	pc.viewport[3] = (float)ext.height;
	pc.cullMode = (uint32_t)view.cullMode;
	pc.drawMode = (uint32_t)view.drawMode;
	pc.numClusters = mNumClusters;
	pc.hizLevels = (uint32_t)mHiZLevels;

	auto copyCounters = [&]()
	{
		cmd->copyBuffer(mView->counters.get(), mReadback.get(), 0, (VkDeviceSize)mSlot * NumCounters * sizeof(uint32_t), NumCounters * sizeof(uint32_t));
		PipelineBarrier()
			.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT)
			.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
		CopyVisibility(cmd, 0, view.viewKey);
	};

	if (view.cullMode == LMCULL_HIZ)
	{
		Cull(cmd, pc, LMPHASE_FIRST);
		Timestamp(cmd, TS_CULL1);
		if (resolve) Resolve(cmd);
		Timestamp(cmd, TS_RES1);
		DrawPhase(lm, view, 0);
		cmd = fb->GetCommands()->GetDrawCommands();
		Timestamp(cmd, TS_DRAW1);
		state->EndRenderPass();
		BuildHiZ(cmd);
		Timestamp(cmd, TS_HIZ);
		Cull(cmd, pc, LMPHASE_SECOND);
		copyCounters();
		Timestamp(cmd, TS_CULL2);
		if (resolve) Resolve(cmd);
		Timestamp(cmd, TS_RES2);
		DrawPhase(lm, view, 1);
		Timestamp(cmd, TS_DRAW2);
	}
	else
	{
		Cull(cmd, pc, LMPHASE_SINGLE);
		copyCounters();
		Timestamp(cmd, TS_CULL1);
		if (resolve) Resolve(cmd);
		Timestamp(cmd, TS_RES1);
		DrawPhase(lm, view, 0);
		Timestamp(cmd, TS_DRAW1);
	}
	return true;
}

//==========================================================================
//
// Visibility readback (ticket 17): the final cull phase's visBits of each
// view, per frame slot, for the next frame's actor gather. BeginFrame runs
// after VulkanRenderDevice::Update waited for every submit, so the copies of
// the previous frame are complete: the gather lags exactly one frame.
//
//==========================================================================

void VkLevelMeshRenderer::CopyVisibility(VulkanCommandBuffer *cmd, unsigned viewIndex, uint64_t viewKey)
{
	auto &slot = mSlots[mSlot];
	if (!mVisReadback || viewKey == 0 || slot.frame != mFrame || viewIndex > MaxPortalViews) return;
	VkDeviceSize offset = ((VkDeviceSize)mSlot * (1 + MaxPortalViews) + viewIndex) * mVisWords * sizeof(uint32_t);
	cmd->copyBuffer(mView->visBits.get(), mVisReadback.get(), 0, offset, mVisWords * sizeof(uint32_t));
	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
	// Views skipped in between (key 0) are never matched.
	for (unsigned i = slot.viewCount; i < viewIndex; i++) slot.viewKeys[i] = 0;
	slot.viewKeys[viewIndex] = viewKey;
	slot.viewCount = std::max(slot.viewCount, viewIndex + 1);
}

void VkLevelMeshRenderer::BeginFrame()
{
	auto &last = mSlots[mSlot];
	mReadSlot = -1;
	if (mVisReadback && last.serial != 0 && last.frame == mFrame && last.viewCount != 0 && last.levelId == mLevelId)
	{
		mReadSlot = mSlot;
		vmaInvalidateAllocation(fb->device->allocator, mVisReadback->allocation,
			(VkDeviceSize)mReadSlot * (1 + MaxPortalViews) * mVisWords * sizeof(uint32_t), (VkDeviceSize)last.viewCount * mVisWords * sizeof(uint32_t));
	}
	mFrame++;
	mPortalViews = 0;
}

const uint32_t *VkLevelMeshRenderer::GetVisibility(uint64_t levelId, uint64_t viewKey, unsigned &words)
{
	if (mReadSlot < 0 || viewKey == 0 || !mVisReadbackData) return nullptr;
	auto &slot = mSlots[mReadSlot];
	if (slot.levelId != levelId) return nullptr;
	for (unsigned i = 0; i < slot.viewCount; i++)
	{
		if (slot.viewKeys[i] != viewKey) continue;
		words = mVisWords;
		return mVisReadbackData + ((size_t)mReadSlot * (1 + MaxPortalViews) + i) * mVisWords;
	}
	return nullptr;
}

//==========================================================================
//
// Timings and counters of completed draws (non-blocking)
//
//==========================================================================

void VkLevelMeshRenderer::Harvest()
{
	double period = fb->device->PhysicalDevice.Properties.Properties.limits.timestampPeriod;
	for (int n = 1; n <= NumSlots; n++)
	{
		int i = (mSlot + n) % NumSlots;	// oldest first
		auto &slot = mSlots[i];
		if (!slot.pending) continue;
		// Without queries there is no availability to poll: a slot is only
		// known complete once a frame boundary waited for its submit.
		if (!mQueries && slot.frame == mFrame) continue;
		uint64_t ts[NumPoints] = {};
		bool ready = true;
		if (mQueries)
		{
			// Only the written points: an unwritten query never becomes available.
			for (int p = 0; p < NumPoints && ready; p++)
			{
				if (slot.points & (1u << p))
					ready = mQueries->getResults(i * NumPoints + p, 1, sizeof(uint64_t), &ts[p], sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
			}
		}
		double portalMs = 0;
		for (unsigned v = 0; v < slot.portalViews && ready && mPortalQueries; v++)
		{
			uint64_t pts[2] = {};
			uint32_t q = i * 2 * MaxPortalViews + 2 * v;
			ready = mPortalQueries->getResults(q, 1, sizeof(uint64_t), &pts[0], sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) &&
				mPortalQueries->getResults(q + 1, 1, sizeof(uint64_t), &pts[1], sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
			if (ready && pts[1] > pts[0]) portalMs += (pts[1] - pts[0]) * period / 1e6;
		}
		if (!ready) continue;
		slot.pending = false;
		if (slot.serial <= mHarvestedSerial) continue;
		mHarvestedSerial = slot.serial;

		const uint32_t *c = mReadbackData + i * NumCounters;
		auto ms = [&](int a, int b) { return (slot.points & (1u << a)) && (slot.points & (1u << b)) && ts[b] > ts[a] ? (ts[b] - ts[a]) * period / 1e6 : 0.0; };
		LevelMeshPerfStats &s = mStats;
		s.serial = slot.serial;
		s.cullMode = slot.cullMode;
		s.drawMode = slot.drawMode;
		for (int p = 0; p < 2; p++)
		{
			s.clusters[p] = c[LMC_CLUSTERS + p];
			uint32_t indices = 0;
			for (int b = 0; b < (int)LMB_DRAWN; b++) indices += c[LMC_INDICES + p * LMB_DRAWN + b];
			s.triangles[p] = indices / 3;
		}
		s.frustumCulled = c[LMC_FRUSTUMCULLED];
		s.occluded = c[LMC_OCCLUDED];
		s.cullMs[0] = ms(TS_START, TS_CULL1);
		s.resolveMs[0] = ms(TS_CULL1, TS_RES1);
		s.drawMs[0] = ms(TS_RES1, TS_DRAW1);
		s.hizMs = ms(TS_DRAW1, TS_HIZ);
		s.cullMs[1] = ms(TS_HIZ, TS_CULL2);
		s.resolveMs[1] = ms(TS_CULL2, TS_RES2);
		s.drawMs[1] = ms(TS_RES2, TS_DRAW2);
		s.totalMs = ms(TS_START, slot.cullMode == LMCULL_HIZ ? TS_DRAW2 : TS_DRAW1);
		s.portalViews = slot.portalViews;
		s.portalMs = portalMs;
	}
}
