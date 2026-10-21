/*
** vk_renderstate_program.cpp
**
** VkRenderState: render program mode (hw_renderprogram.h)
**
** A program draw binds its own pipeline: sets 0 and 1 and the push constant
** range of the program pipeline layout are
** compatible with the table shader layout, set 2 is an unused empty set and
** set 3 holds the program bindings. Leaving program mode forces everything the
** table shader path caches (pipeline, material set, buffers) to be rebound.
**
**---------------------------------------------------------------------------
**
** Copyright 2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include "vk_renderstate.h"
#include "vk_renderprogram.h"
#include "vulkan/system/vk_renderdevice.h"
#include "vulkan/system/vk_commandbuffer.h"
#include "vulkan/system/vk_hwbuffer.h"
#include "vulkan/renderer/vk_descriptorset.h"
#include "vulkan/textures/vk_renderbuffers.h"
#include "vulkan/textures/vk_hwtexture.h"
#include "vulkan/textures/vk_samplers.h"
#include "vulkan/textures/vk_texture.h"
#include "hw_clock.h"
#include "engineerrors.h"
#include <deque>

EXTERN_CVAR(Int, vk_submit_size)

//==========================================================================
//
// Render target dimensions and mode switching
//
//==========================================================================

VkRenderState::~VkRenderState()
{
	if (mProgramUniformRing && mProgramUniformRingMap)
		mProgramUniformRing->Unmap();
	if (mProgramNullTexelView != VK_NULL_HANDLE)
		vkDestroyBufferView(fb->device->device, mProgramNullTexelView, nullptr);
}

int VkRenderState::ProgramTargetWidth() const
{
	return mUserTarget ? mUserTarget->Width() : mRenderTarget.Width;
}

int VkRenderState::ProgramTargetHeight() const
{
	return mUserTarget ? mUserTarget->Height() : mRenderTarget.Height;
}

void VkRenderState::OnSetProgram(IRenderProgram *prog)
{
	if (prog == nullptr && mInProgramMode) LeaveProgramMode();
}

void VkRenderState::LeaveProgramMode()
{
	mInProgramMode = false;

	if (mUserTarget)
	{
		EndRenderPass();
		mUserTarget = nullptr;
	}

	// Everything the program pipeline may have replaced is rebound by the next table shader draw.
	mPipelineKey = {};
	mLastViewpointOffset = 0xffffffff;
	mLastVertexBuffer = nullptr;
	mLastIndexBuffer = nullptr;
	mLastProgramPipeline = nullptr;
	mProgramLastVB = nullptr;
	mProgramLastInstanceVB = nullptr;
	// The material pointer may be stale (its FMaterial can be deleted after its last draw, e.g. the start screen's): rebind
	// the null texture set; every textured table draw calls SetMaterial anyway.
	mMaterial.Reset();
	mMaterial.mChanged = true;
	mScissorChanged = true;
	mViewportChanged = true;
	mStencilRefChanged = true;
	mBias.mChanged = true;
	mNeedApply = true;
}

void VkRenderState::SetRenderTarget(IRenderTarget *target)
{
	auto rt = static_cast<VkProgramRenderTarget*>(target);
	if (rt == mUserTarget) return;

	// Clear() and ClearRenderTarget() only record the clear for the next render pass: run it on the target it was meant for
	// (GL clears immediately), otherwise a target that is cleared and then left before any draw keeps its old content and
	// the clear lands on the next target instead.
	if (mClearTargets != 0 && !mCommandBuffer && (mUserTarget || mRenderTarget.Image))
	{
		mCommandBuffer = fb->GetCommands()->GetDrawCommands();
		BeginRenderPass(mCommandBuffer);
	}
	EndRenderPass();
	mUserTarget = rt;
	mLastProgramPipeline = nullptr;
	mScissorChanged = true;
	mViewportChanged = true;
	mStencilRefChanged = true;
	mBias.mChanged = true;
	mNeedApply = true;
	if (!rt)
	{
		mPipelineKey = {};
		mLastViewpointOffset = 0xffffffff;
		mLastVertexBuffer = nullptr;
		mLastIndexBuffer = nullptr;
		mMaterial.Reset();
		mMaterial.mChanged = true;
	}
}

void VkRenderState::ClearRenderTarget(float r, float g, float b, float a)
{
	mClearColor[0] = r;
	mClearColor[1] = g;
	mClearColor[2] = b;
	mClearColor[3] = a;
	mClearColorCustom = true;
	mClearTargets |= CT_Color;
	EndRenderPass();
}

void VkRenderState::BeginProgramTargetRenderPass(VulkanCommandBuffer *cmdbuffer)
{
	auto rt = mUserTarget;
	auto buffers = fb->GetBuffers();

	for (int i = 0; i < rt->NumColor(); i++)
		VkProgramTransitionImage(cmdbuffer, &rt->Color(i), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	if (rt->HasDepth())
	{
		if (rt->OwnsDepth())
		{
			VkProgramTransitionImage(cmdbuffer, &rt->Depth(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		}
		else if (mSceneDepthSampled)
		{
			PipelineBarrier()
				.AddImage(buffers->SceneDepthStencil.Image.get(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
				.Execute(cmdbuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
			mSceneDepthSampled = false;
		}
	}

	int clearTargets = mClearTargets;
	if (!rt->HasDepth()) clearTargets &= ~(CT_Depth | CT_Stencil);

	std::vector<VkClearValue> clearValues;
	for (int i = 0; i < rt->NumColor(); i++)
	{
		VkClearValue v = {};
		if (mClearColorCustom)
		{
			if (VkProgramRenderTarget::IsInteger(rt->Desc().colorFormats[i]))
			{
				v.color.uint32[0] = (uint32_t)mClearColor[0];
				v.color.uint32[1] = (uint32_t)mClearColor[1];
				v.color.uint32[2] = (uint32_t)mClearColor[2];
				v.color.uint32[3] = (uint32_t)mClearColor[3];
			}
			else
			{
				v.color.float32[0] = mClearColor[0];
				v.color.float32[1] = mClearColor[1];
				v.color.float32[2] = mClearColor[2];
				v.color.float32[3] = mClearColor[3];
			}
		}
		clearValues.push_back(v);
	}
	if (rt->HasDepth())
	{
		VkClearValue v = {};
		v.depthStencil.depth = 1.0f;
		v.depthStencil.stencil = 0;
		clearValues.push_back(v);
	}

	VkRenderPassBeginInfo info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	info.renderPass = rt->GetRenderPass(clearTargets)->renderPass;
	info.framebuffer = rt->GetFramebuffer()->framebuffer;
	info.renderArea.offset = { 0, 0 };
	info.renderArea.extent = { (uint32_t)rt->Width(), (uint32_t)rt->Height() };
	info.clearValueCount = (uint32_t)clearValues.size();
	info.pClearValues = clearValues.data();
	cmdbuffer->beginRenderPass(&info, VK_SUBPASS_CONTENTS_INLINE);

	mClearTargets = 0;
	mClearColorCustom = false;
}

//==========================================================================
//
// Resources: layout transitions for render targets / scene copies
//
//==========================================================================

bool VkRenderState::PrepareProgramResources(VkRenderProgram *program, bool compute)
{
	auto buffers = fb->GetBuffers();

	for (auto &b : program->Desc().bindings)
	{
		const auto &slot = mProgramSlots[b.binding];

		if (slot.type == FProgramBindingSlot::RenderTargetTexture && slot.target)
		{
			auto rt = static_cast<VkProgramRenderTarget*>(slot.target);
			VkTextureImage *image;
			VkImageLayout layout;
			if (b.kind == RenderProgramBinding::Image2D)
			{
				image = &rt->Color(slot.attachment);
				layout = VK_IMAGE_LAYOUT_GENERAL;
			}
			else if (slot.attachment < 0 && rt->OwnsDepth())
			{
				image = &rt->Depth();
				layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			}
			else
			{
				image = &rt->Color(slot.attachment);
				layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}

			if (rt == mUserTarget && !compute) continue;	// sampling the current target is a feedback loop; leave the layout alone
			if (image->Layout != layout)
			{
				EndRenderPass();
				VkProgramTransitionImage(fb->GetCommands()->GetDrawCommands(), image, layout);
			}
		}
		else if (slot.type == FProgramBindingSlot::SceneDepthTexture)
		{
			if (!mSceneDepthSampled)
			{
				EndRenderPass();
				PipelineBarrier()
					.AddImage(buffers->SceneDepthStencil.Image.get(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
					.Execute(fb->GetCommands()->GetDrawCommands(), VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
				mSceneDepthSampled = true;
			}
		}
	}
	return true;
}

//==========================================================================
//
// Uniform ring
//
//==========================================================================

bool VkRenderState::WriteProgramUniforms(const void *data, size_t size, uint32_t &offset)
{
	auto &limits = fb->device->PhysicalDevice.Properties.Properties.limits;
	size_t align = max((size_t)limits.minUniformBufferOffsetAlignment, (size_t)16);
	size_t asize = (size + align - 1) / align * align;

	if (!mProgramUniformRing)
	{
		mProgramUniformRingSize = 4 * 1024 * 1024;
		mProgramUniformRing = BufferBuilder()
			.Usage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_UNKNOWN, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
			.MemoryType(
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
			.Size(mProgramUniformRingSize)
			.DebugName("VkRenderState.ProgramUniformRing")
			.Create(fb->device.get());
		mProgramUniformRingMap = (uint8_t*)mProgramUniformRing->Map(0, mProgramUniformRingSize);
		mProgramUniformRingPos = 0;
	}

	if (mProgramUniformRingPos + asize > mProgramUniformRingSize)
		return false;

	memcpy(mProgramUniformRingMap + mProgramUniformRingPos, data, size);
	offset = (uint32_t)mProgramUniformRingPos;
	mProgramUniformRingPos += asize;
	return true;
}

//==========================================================================
//
// Set 3: program bindings
//
//==========================================================================

VulkanDescriptorSet *VkRenderState::UpdateProgramDescriptorSet(VkRenderProgram *program, std::vector<uint32_t> &dynamicOffsets, bool compute)
{
	const auto &desc = program->Desc();
	auto buffers = fb->GetBuffers();
	auto samplers = fb->GetSamplerManager();
	auto &limits = fb->device->PhysicalDevice.Properties.Properties.limits;
	size_t uniformAlign = max((size_t)limits.minUniformBufferOffsetAlignment, (size_t)16);

	// make sure the ring exists
	uint32_t dummyOffset = 0;
	if (!mProgramUniformRing) WriteProgramUniforms(&dummyOffset, 0, dummyOffset);

	struct Entry
	{
		VkDescriptorType type;
		VulkanBuffer *buffer = nullptr;
		VkDeviceSize bufferOffset = 0, bufferRange = 0;
		VkBufferView texelView = VK_NULL_HANDLE;
		VulkanImageView *view = nullptr;
		VulkanSampler *sampler = nullptr;
		VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		int binding = 0;
		const void *uniformData = nullptr;
		size_t uniformSize = 0;
		uint32_t dynamicOffset = 0;
	};
	std::vector<Entry> entries;
	std::vector<uintptr_t> signature;
	size_t ringBytes = 0;

	auto nullView = fb->GetTextureManager()->GetNullTextureView();
	auto nullSampler = samplers->Get(CLAMP_NOFILTER_XY);

	// A: collect the entries and the signature that identifies the descriptor set contents
	for (int idx : program->SortedBindings())
	{
		const auto &b = desc.bindings[idx];
		const auto &slot = mProgramSlots[b.binding];
		Entry e;
		e.binding = b.binding;
		e.type = VkRenderProgram::DescriptorTypeFor(b);

		switch (b.kind)
		{
		case RenderProgramBinding::UniformBlock:
		case RenderProgramBinding::StorageBuffer:
			e.buffer = mProgramUniformRing.get();
			e.bufferRange = max((size_t)16, (size_t)min((VkDeviceSize)mProgramUniformRingSize, (VkDeviceSize)(b.kind == RenderProgramBinding::UniformBlock ? limits.maxUniformBufferRange : mProgramUniformRingSize)));
			if (b.kind == RenderProgramBinding::UniformBlock && slot.type == FProgramBindingSlot::Uniforms)
			{
				e.uniformData = &mProgramUniformData[slot.uniformsOffset];
				e.uniformSize = slot.uniformsSize;
				e.bufferRange = slot.uniformsSize;
				ringBytes += (slot.uniformsSize + uniformAlign - 1) / uniformAlign * uniformAlign;
			}
			else if (slot.type == FProgramBindingSlot::Buffer && slot.buffer)
			{
				auto hwbuf = dynamic_cast<VkHardwareBuffer*>(slot.buffer);
				if (hwbuf && hwbuf->mBuffer)
				{
					e.buffer = hwbuf->mBuffer.get();
					e.bufferRange = slot.size ? slot.size : hwbuf->mBuffer->size - slot.offset;
					e.dynamicOffset = (uint32_t)slot.offset;
				}
			}
			else
			{
				e.bufferRange = min((VkDeviceSize)e.bufferRange, (VkDeviceSize)256);
			}
			signature.push_back((uintptr_t)e.buffer);
			signature.push_back((uintptr_t)e.bufferRange);
			break;

		case RenderProgramBinding::Sampler2D:
		case RenderProgramBinding::USampler2D:
		case RenderProgramBinding::Sampler2DArray:
		case RenderProgramBinding::USampler2DArray:
		{
			bool isInteger = b.kind == RenderProgramBinding::USampler2D || b.kind == RenderProgramBinding::USampler2DArray;
			e.view = nullView;
			e.sampler = nullSampler;
			if (b.kind == RenderProgramBinding::Sampler2DArray || b.kind == RenderProgramBinding::USampler2DArray)
			{
				// An array sampler needs an array view even when nothing is bound.
				if (!mProgramNullArray)
				{
					static const unsigned char zero[4] = {};
					mProgramNullArray.reset(new VkTextureArray(fb, 1, 1, 1, 4, false, false, "VkRenderState.NullArray"));
					mProgramNullArray->UploadLayer(0, zero, 1, 1);
				}
				e.view = static_cast<VkTextureArray*>(mProgramNullArray.get())->GetView();
			}
			if (slot.type == FProgramBindingSlot::Texture && slot.texture)
			{
				int clampmode = slot.clampmode;
				if (isInteger) clampmode = CLAMP_NOFILTER_XY;
				else if (slot.filter == 1 && clampmode < CLAMP_NOFILTER) clampmode += CLAMP_NOFILTER - CLAMP_NONE;
				clampmode = clamp(clampmode, 0, NUMSAMPLERS - 1);
				e.sampler = samplers->Get(clampmode);

				if (auto arr = dynamic_cast<VkTextureArray*>(slot.texture))
				{
					e.view = arr->GetView();
				}
				else if (auto hw = dynamic_cast<VkHardwareTexture*>(slot.texture))
				{
					if (auto image = hw->GetCreatedImage()) e.view = image->View.get();
				}
			}
			else if (slot.type == FProgramBindingSlot::RenderTargetTexture && slot.target)
			{
				auto rt = static_cast<VkProgramRenderTarget*>(slot.target);
				e.sampler = samplers->Get(CLAMP_NOFILTER_XY);
				if (slot.attachment < 0 && rt->OwnsDepth())
				{
					e.view = rt->Depth().DepthOnlyView.get();
					e.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
				}
				else
				{
					e.view = rt->Color(slot.attachment).View.get();
				}
			}
			else if (slot.type == FProgramBindingSlot::SceneColorTexture)
			{
				e.sampler = samplers->Get(CLAMP_XY_NOMIP);
				if (mSceneCopy.View) e.view = mSceneCopy.View.get();
			}
			else if (slot.type == FProgramBindingSlot::ShadowMapTexture)
			{
				// UpdateShadowMap leaves it in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				e.sampler = samplers->ShadowmapSampler.get();
				e.view = fb->GetTextureManager()->Shadowmap.View.get();
			}
			else if (slot.type == FProgramBindingSlot::SceneDepthTexture)
			{
				e.sampler = samplers->Get(CLAMP_NOFILTER_XY);
				if (buffers->SceneDepthStencil.DepthOnlyView)
				{
					e.view = buffers->SceneDepthStencil.DepthOnlyView.get();
					e.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
				}
			}
			signature.push_back((uintptr_t)e.view);
			signature.push_back((uintptr_t)e.sampler);
			break;
		}

		case RenderProgramBinding::SamplerBuffer:
		case RenderProgramBinding::USamplerBuffer:
		case RenderProgramBinding::ISamplerBuffer:
		{
			if (slot.type == FProgramBindingSlot::TexelBuffer && slot.buffer)
			{
				if (auto tb = dynamic_cast<VkTexelBuffer*>(slot.buffer))
				{
					e.texelView = tb->GetView();
					signature.push_back((uintptr_t)tb);
					signature.push_back((uintptr_t)tb->Version());
				}
			}
			if (e.texelView == VK_NULL_HANDLE)
			{
				if (!mProgramNullTexelBuffer)
				{
					mProgramNullTexelBuffer = BufferBuilder()
						.Usage(VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY)
						.Size(64)
						.DebugName("VkRenderState.NullTexelBuffer")
						.Create(fb->device.get());
					fb->GetCommands()->GetTransferCommands()->fillBuffer(mProgramNullTexelBuffer->buffer, 0, VK_WHOLE_SIZE, 0);
					VkBufferViewCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
					info.buffer = mProgramNullTexelBuffer->buffer;
					info.format = VK_FORMAT_R32G32B32A32_SFLOAT;
					info.range = VK_WHOLE_SIZE;
					CheckVulkanError(vkCreateBufferView(fb->device->device, &info, nullptr, &mProgramNullTexelView), "Could not create texel buffer view");
				}
				e.texelView = mProgramNullTexelView;
				signature.push_back(0);
				signature.push_back(0);
			}
			signature.push_back((uintptr_t)e.texelView);
			break;
		}

		case RenderProgramBinding::Image2D:
		{
			e.layout = VK_IMAGE_LAYOUT_GENERAL;
			e.view = nullView;
			if (slot.type == FProgramBindingSlot::RenderTargetTexture && slot.target)
				e.view = static_cast<VkProgramRenderTarget*>(slot.target)->Color(slot.attachment).View.get();
			signature.push_back((uintptr_t)e.view);
			break;
		}
		}
		entries.push_back(e);
	}

	// C: make sure the ring has room for this draw's uniform blocks
	if (mProgramUniformRingPos + ringBytes > mProgramUniformRingSize)
		WaitForStreamBuffers();

	// B: a descriptor set holds the entries; reuse the last one if nothing changed
	int generation = fb->GetCommands()->DeleteGeneration;
	bool needNewSet = !mProgramSet || mProgramSetProgram != program || mProgramSetGeneration != generation || mProgramSetSignature != signature;
	std::unique_ptr<VulkanDescriptorSet> newSet;
	if (needNewSet)
	{
		if (!mProgramDescriptorPool)
		{
			const int maxSets = 1024;
			mProgramDescriptorPool = DescriptorPoolBuilder()
				.MaxSets(maxSets)
				.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets * 4)
				.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, maxSets * 4)
				.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 16)
				.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, maxSets * 8)
				.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxSets * 4)
				.DebugName("VkRenderState.ProgramDescriptorPool")
				.Create(fb->device.get());
		}
		newSet = mProgramDescriptorPool->tryAllocate(program->GetSetLayout());
		if (!newSet)
		{
			WaitForStreamBuffers();
			newSet = mProgramDescriptorPool->tryAllocate(program->GetSetLayout());
			if (!newSet) I_FatalError("Render program %s: out of descriptor sets", desc.name.GetChars());
		}
		newSet->SetDebugName("VkRenderState.ProgramSet");
		generation = fb->GetCommands()->DeleteGeneration;
	}

	// D: write the uniform data
	dynamicOffsets.clear();
	for (auto &e : entries)
	{
		if (e.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || e.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
		{
			if (e.uniformData)
			{
				uint32_t offset = 0;
				if (!WriteProgramUniforms(e.uniformData, e.uniformSize, offset))
					I_FatalError("Render program %s: uniform ring overflow", desc.name.GetChars());
				e.dynamicOffset = offset;
			}
			dynamicOffsets.push_back(e.dynamicOffset);
		}
	}

	// E: update the descriptors of a new set
	if (needNewSet)
	{
		std::vector<VkWriteDescriptorSet> writes;
		std::deque<VkDescriptorBufferInfo> bufferInfos;
		std::deque<VkDescriptorImageInfo> imageInfos;
		for (auto &e : entries)
		{
			VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			w.dstSet = newSet->set;
			w.dstBinding = e.binding;
			w.descriptorCount = 1;
			w.descriptorType = e.type;
			switch (e.type)
			{
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
				bufferInfos.push_back({ e.buffer->buffer, 0, e.bufferRange });
				w.pBufferInfo = &bufferInfos.back();
				break;
			case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
				w.pTexelBufferView = &e.texelView;
				break;
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
				imageInfos.push_back({ VK_NULL_HANDLE, e.view->view, e.layout });
				w.pImageInfo = &imageInfos.back();
				break;
			default:
				imageInfos.push_back({ e.sampler->sampler, e.view->view, e.layout });
				w.pImageInfo = &imageInfos.back();
				break;
			}
			writes.push_back(w);
		}
		vkUpdateDescriptorSets(fb->device->device, (uint32_t)writes.size(), writes.data(), 0, nullptr);

		mProgramSet = newSet.get();
		mProgramSetSignature = std::move(signature);
		mProgramSetProgram = program;
		mProgramSetGeneration = generation;
		fb->GetCommands()->DrawDeleteList->Add(std::move(newSet));
	}

	return mProgramSet;
}

//==========================================================================
//
// Apply for programs
//
//==========================================================================

bool VkRenderState::ApplyProgram(int dt)
{
	auto program = static_cast<VkRenderProgram*>(mProgram);
	if (!program || program->Desc().isCompute) return false;

	drawcalls.Clock();
	mInProgramMode = true;

	mApplyCount++;
	if (mApplyCount >= vk_submit_size)
	{
		fb->GetCommands()->FlushCommands(false);
		mApplyCount = 0;
	}

	if (mUserTarget && mUserTarget->Validate())
	{
		EndRenderPass();
		mProgramSet = nullptr;
	}

	PrepareProgramResources(program, false);

	std::vector<uint32_t> dynamicOffsets;
	VulkanDescriptorSet *set = UpdateProgramDescriptorSet(program, dynamicOffsets, false);

	if (!mCommandBuffer)
	{
		mCommandBuffer = fb->GetCommands()->GetDrawCommands();
		mScissorChanged = true;
		mViewportChanged = true;
		mStencilRefChanged = true;
		mBias.mChanged = true;
		mLastProgramPipeline = nullptr;
		mProgramLastVB = nullptr;
		mProgramLastInstanceVB = nullptr;
		BeginRenderPass(mCommandBuffer);
	}

	// pipeline
	VkProgramPipelineKey key;
	memset(&key, 0, sizeof(key));
	VulkanRenderPass *renderPass;
	bool hasDepth;
	if (mUserTarget)
	{
		renderPass = mUserTarget->GetRenderPass(0);
		key.NumColorAttachments = mUserTarget->NumColor();
		key.Samples = VK_SAMPLE_COUNT_1_BIT;
		hasDepth = mUserTarget->HasDepth();
	}
	else
	{
		renderPass = GetRenderPassSetup()->GetRenderPass(0);
		key.NumColorAttachments = mRenderTarget.DrawBuffers;
		key.Samples = mRenderTarget.Samples;
		hasDepth = mRenderTarget.DepthStencil != nullptr;
	}
	key.RenderPass = (uint64_t)(uintptr_t)renderPass;
	key.DrawType = dt;
	key.DepthTest = hasDepth && mDepthTest;
	key.DepthWrite = hasDepth && mDepthTest && mDepthWrite;
	key.DepthFunc = mDepthFunc;
	key.DepthClamp = mDepthClamp;
	key.DepthBias = !(mBias.mFactor == 0 && mBias.mUnits == 0);
	key.StencilTest = hasDepth && mStencilTest;
	key.StencilPassOp = mStencilOp;
	key.ColorMask = mColorMask;
	key.CullMode = mCullMode;
	if (mCustomBlend.enabled)
	{
		key.CustomBlend = mCustomBlend.blend ? 1 : 2;
		key.Att[0] = { mCustomBlend.blend, mCustomBlend.srcRGB, mCustomBlend.dstRGB, mCustomBlend.srcA, mCustomBlend.dstA, mCustomBlend.equationRGB, mCustomBlend.equationA };
		key.NumAtt = mCustomBlend.numAttachments;
		for (int i = 0; i < mCustomBlend.numAttachments && i < 3; i++)
		{
			const auto &a = mCustomBlend.attachment[i];
			key.Att[1 + i] = { a.blend, a.srcRGB, a.dstRGB, a.srcA, a.dstA, a.equationRGB, a.equationA };
		}
	}
	else
	{
		key.RenderStyle = mRenderStyle;
	}

	auto pipeline = program->GetGraphicsPipeline(key, renderPass);
	if (pipeline != mLastProgramPipeline)
	{
		mCommandBuffer->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		mLastProgramPipeline = pipeline;
	}

	ApplyScissor();
	ApplyViewport();
	ApplyStencilRef();
	ApplyDepthBias();

	// vertex / instance / index buffers
	const auto &desc = program->Desc();
	auto vb = static_cast<VkHardwareVertexBuffer*>(mVertexBuffer);
	auto ivb = static_cast<VkHardwareVertexBuffer*>(mInstanceBuffer);
	if (vb != mProgramLastVB || ivb != mProgramLastInstanceVB || mVertexOffsets[0] != mProgramLastVBOffset || mInstanceOffset != mProgramLastInstanceOffset)
	{
		VkBuffer buffers[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
		VkDeviceSize offsets[2] = { 0, 0 };
		uint32_t count = 0;
		VulkanBuffer *first = vb ? vb->mBuffer.get() : nullptr;
		VulkanBuffer *second = ivb ? ivb->mBuffer.get() : nullptr;
		if (desc.numBindingPoints >= 1 && first)
		{
			buffers[0] = first->buffer;
			offsets[0] = (VkDeviceSize)mVertexOffsets[0] * desc.vertexStride[0];
			count = 1;
			if (desc.numBindingPoints >= 2 && second)
			{
				buffers[1] = second->buffer;
				offsets[1] = (VkDeviceSize)mInstanceOffset * desc.vertexStride[1];
				count = 2;
			}
			mCommandBuffer->bindVertexBuffers(0, count, buffers, offsets);
		}
		else if (desc.numBindingPoints >= 2 && second)
		{
			offsets[1] = (VkDeviceSize)mInstanceOffset * desc.vertexStride[1];
			mCommandBuffer->bindVertexBuffers(1, 1, &second->buffer, &offsets[1]);
		}
		mProgramLastVB = vb;
		mProgramLastInstanceVB = ivb;
		mProgramLastVBOffset = mVertexOffsets[0];
		mProgramLastInstanceOffset = mInstanceOffset;
	}
	if (mIndexBuffer != mLastIndexBuffer && mIndexBuffer)
	{
		mCommandBuffer->bindIndexBuffer(static_cast<VkHardwareIndexBuffer*>(mIndexBuffer)->mBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
		mLastIndexBuffer = mIndexBuffer;
	}

	if (desc.rayQuery)
		mCommandBuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, program->GetPipelineLayout(), 0, fb->GetDescriptorSetManager()->GetFixedDescriptorSet());
	mCommandBuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, program->GetPipelineLayout(), 3, set, (uint32_t)dynamicOffsets.size(), dynamicOffsets.data());

	mProgramChanged = false;
	mNeedApply = false;
	drawcalls.Unclock();
	return true;
}

void VkRenderState::DrawInstanced(int dt, int index, int count, int instanceCount)
{
	if (mProgram)
	{
		if (ApplyProgram(dt)) mCommandBuffer->draw(count, instanceCount, index, 0);
		return;
	}
	Apply(dt);
	mCommandBuffer->draw(count, instanceCount, index, 0);
}

void VkRenderState::DrawIndexedInstanced(int dt, int index, int count, int instanceCount)
{
	if (mProgram)
	{
		if (ApplyProgram(dt)) mCommandBuffer->drawIndexed(count, instanceCount, index, 0, 0);
		return;
	}
	Apply(dt);
	mCommandBuffer->drawIndexed(count, instanceCount, index, 0, 0);
}

//==========================================================================
//
// Scene colour copy
//
//==========================================================================

void VkRenderState::CopySceneColor()
{
	auto buffers = fb->GetBuffers();
	int w = buffers->GetWidth(), h = buffers->GetHeight();

	EndRenderPass();
	auto cmd = fb->GetCommands()->GetDrawCommands();

	if (!mSceneCopy.Image || mSceneCopyWidth != w || mSceneCopyHeight != h)
	{
		mSceneCopy.Reset(fb);
		mSceneCopy.Image = ImageBuilder()
			.Format(VK_FORMAT_R16G16B16A16_SFLOAT)
			.Size(w, h)
			.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
			.DebugName("VkRenderState.SceneCopy")
			.Create(fb->device.get());
		mSceneCopy.View = ImageViewBuilder()
			.Image(mSceneCopy.Image.get(), VK_FORMAT_R16G16B16A16_SFLOAT)
			.DebugName("VkRenderState.SceneCopyView")
			.Create(fb->device.get());
		mSceneCopy.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
		mSceneCopyWidth = w;
		mSceneCopyHeight = h;
		mProgramSet = nullptr;
	}

	VkImage src = buffers->SceneColor.Image->image;
	PipelineBarrier()
		.AddImage(src, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	VkProgramTransitionImage(cmd, &mSceneCopy, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	if (buffers->GetSceneSamples() != VK_SAMPLE_COUNT_1_BIT)
	{
		VkImageResolve region = {};
		region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		region.extent = { (uint32_t)w, (uint32_t)h, 1 };
		cmd->resolveImage(src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mSceneCopy.Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	}
	else
	{
		VkImageCopy region = {};
		region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		region.extent = { (uint32_t)w, (uint32_t)h, 1 };
		cmd->copyImage(src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mSceneCopy.Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	}

	PipelineBarrier()
		.AddImage(src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	VkProgramTransitionImage(cmd, &mSceneCopy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

//==========================================================================
//
// Compute
//
//==========================================================================

void VkRenderState::DispatchCompute(VkRenderProgram *program, int gx, int gy, int gz)
{
	if (!program || !program->Desc().isCompute) return;

	EndRenderPass();
	PrepareProgramResources(program, true);

	std::vector<uint32_t> dynamicOffsets;
	VulkanDescriptorSet *set = UpdateProgramDescriptorSet(program, dynamicOffsets, true);

	auto cmd = fb->GetCommands()->GetDrawCommands();
	cmd->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, program->GetComputePipeline());
	cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, program->GetPipelineLayout(), 3, set, (uint32_t)dynamicOffsets.size(), dynamicOffsets.data());
	cmd->dispatch(gx, gy, gz);
}

void VkRenderState::MemoryBarrierCompute()
{
	EndRenderPass();
	PipelineBarrier()
		.AddMemory(VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT)
		.Execute(fb->GetCommands()->GetDrawCommands(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
}
