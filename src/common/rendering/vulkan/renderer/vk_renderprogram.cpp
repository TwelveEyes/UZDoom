/*
** vk_renderprogram.cpp
**
** Vulkan implementation of the generic render program API (hw_renderprogram.h)
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

#include "vk_renderprogram.h"
#include "vulkan/system/vk_renderdevice.h"
#include "vulkan/system/vk_commandbuffer.h"
#include "vulkan/renderer/vk_renderpass.h"
#include "vulkan/renderer/vk_descriptorset.h"
#include "vulkan/textures/vk_renderbuffers.h"
#include "vulkan/shaders/vk_shader.h"
#include "engineerrors.h"
#include "printf.h"
#include <algorithm>

//==========================================================================
//
// Helpers
//
//==========================================================================

static VkBlendFactor BlendFactorToVk(int f)
{
	static const VkBlendFactor table[] = {
		VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
		VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
		VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA };
	return table[clamp(f, 0, 9)];
}

static VkBlendOp BlendEquationToVk(int e)
{
	static const VkBlendOp table[] = { VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_MIN, VK_BLEND_OP_MAX };
	return table[clamp(e, 0, 4)];
}

static VkFormat VertexFormatToVk(int format)
{
	switch (format)
	{
	case VFmt_Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
	case VFmt_Float3: return VK_FORMAT_R32G32B32_SFLOAT;
	case VFmt_Float2: return VK_FORMAT_R32G32_SFLOAT;
	case VFmt_Float: return VK_FORMAT_R32_SFLOAT;
	case VFmt_Byte4: return VK_FORMAT_R8G8B8A8_UNORM;
	case VFmt_Packed_A2R10G10B10: return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
	case VFmt_Byte4_UInt: return VK_FORMAT_R8G8B8A8_UINT;
	case RPFmt_UInt: return VK_FORMAT_R32_UINT;
	case RPFmt_Int: return VK_FORMAT_R32_SINT;
	case RPFmt_UInt2: return VK_FORMAT_R32G32_UINT;
	case RPFmt_UInt4: return VK_FORMAT_R32G32B32A32_UINT;
	}
	return VK_FORMAT_R32G32B32A32_SFLOAT;
}

static int MipLevels(int w, int h)
{
	int levels = 1;
	while (w > 1 || h > 1)
	{
		w = max(w >> 1, 1);
		h = max(h >> 1, 1);
		levels++;
	}
	return levels;
}

void VkProgramTransitionImage(VulkanCommandBuffer *cmd, VkTextureImage *image, VkImageLayout newLayout)
{
	if (image->Layout == newLayout) return;

	bool fromUndefined = image->Layout == VK_IMAGE_LAYOUT_UNDEFINED;
	PipelineBarrier()
		.AddImage(image->Image.get(), image->Layout, newLayout,
			fromUndefined ? 0 : (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT),
			VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
			image->AspectMask, 0, image->Image->mipLevels)
		.Execute(cmd, fromUndefined ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	image->Layout = newLayout;
}

//==========================================================================
//
// VkRenderProgram
//
//==========================================================================

VkDescriptorType VkRenderProgram::DescriptorTypeFor(const RenderProgramBinding &b)
{
	switch (b.kind)
	{
	case RenderProgramBinding::UniformBlock: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	case RenderProgramBinding::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	case RenderProgramBinding::SamplerBuffer:
	case RenderProgramBinding::USamplerBuffer:
	case RenderProgramBinding::ISamplerBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
	case RenderProgramBinding::Image2D: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	default: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	}
}

std::unique_ptr<VulkanShader> VkRenderProgram::CompileStage(ShaderType type, const FString &source)
{
	const char *stage = type == ShaderType::Vertex ? "vertex" : type == ShaderType::Fragment ? "fragment" : "compute";
	FString shadername;
	shadername.Format("%s.%s", mDesc.name.GetChars(), stage);
	try
	{
		return ShaderBuilder()
			.Type(type)
			.AddSource(shadername.GetChars(), source.GetChars())
			.DebugName(shadername.GetChars())
			.Create(shadername.GetChars(), fb->device.get());
	}
	catch (const std::exception &e)
	{
		I_FatalError("Render program %s: %s", mDesc.name.GetChars(), e.what());
	}
	return nullptr;
}

VkRenderProgram::VkRenderProgram(VulkanRenderDevice *fb, const RenderProgramDesc &desc) : fb(fb), mDesc(desc)
{
	if (desc.isCompute)
	{
		mCompute = CompileStage(ShaderType::Compute, GenerateProgramPrelude(desc, true, false, true) + desc.computeSource);
	}
	else
	{
		mVertex = CompileStage(ShaderType::Vertex, GenerateProgramPrelude(desc, true, false, false) + desc.vertexSource);
		mFragment = CompileStage(ShaderType::Fragment, GenerateProgramPrelude(desc, true, true, false) + desc.fragmentSource);
	}

	// Set 3: the program bindings
	DescriptorSetLayoutBuilder setBuilder;
	for (auto &b : desc.bindings)
	{
		VkShaderStageFlags stages = 0;
		if (b.vertex) stages |= VK_SHADER_STAGE_VERTEX_BIT;
		if (b.fragment) stages |= VK_SHADER_STAGE_FRAGMENT_BIT;
		if (b.compute) stages |= VK_SHADER_STAGE_COMPUTE_BIT;
		if (!stages) stages = desc.isCompute ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
		setBuilder.AddBinding(b.binding, DescriptorTypeFor(b), 1, stages);
	}
	setBuilder.DebugName("VkRenderProgram.SetLayout");
	mSetLayout = setBuilder.Create(fb->device.get());

	// Set 2 is unused by programs; the pipeline layout must be contiguous.
	mEmptyLayout = DescriptorSetLayoutBuilder().DebugName("VkRenderProgram.EmptyLayout").Create(fb->device.get());

	// Sets 0 and 1 and the push constants are layout compatible with the table shader pipelines.
	auto descriptors = fb->GetDescriptorSetManager();
	PipelineLayoutBuilder layoutBuilder;
	layoutBuilder.AddSetLayout(descriptors->GetFixedSetLayout());
	layoutBuilder.AddSetLayout(descriptors->GetHWBufferSetLayout());
	layoutBuilder.AddSetLayout(mEmptyLayout.get());
	layoutBuilder.AddSetLayout(mSetLayout.get());
	layoutBuilder.AddPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants));
	layoutBuilder.DebugName("VkRenderProgram.PipelineLayout");
	mPipelineLayout = layoutBuilder.Create(fb->device.get());

	for (int i = 0; i < (int)desc.bindings.Size(); i++)
		mSortedBindings.push_back(i);
	std::sort(mSortedBindings.begin(), mSortedBindings.end(), [&](int a, int b) { return desc.bindings[a].binding < desc.bindings[b].binding; });
}

VkRenderProgram::~VkRenderProgram()
{
}

VulkanPipeline *VkRenderProgram::GetComputePipeline()
{
	if (!mComputePipeline)
	{
		mComputePipeline = ComputePipelineBuilder()
			.Cache(fb->GetRenderPassManager()->GetCache())
			.Layout(mPipelineLayout.get())
			.ComputeShader(mCompute.get())
			.DebugName(mDesc.name.GetChars())
			.Create(fb->device.get());
	}
	return mComputePipeline.get();
}

VulkanPipeline *VkRenderProgram::GetGraphicsPipeline(const VkProgramPipelineKey &key, VulkanRenderPass *renderPass)
{
	auto &item = mPipelines[key];
	if (item) return item.get();

	GraphicsPipelineBuilder builder;
	builder.Cache(fb->GetRenderPassManager()->GetCache());
	builder.AddVertexShader(mVertex.get());
	builder.AddFragmentShader(mFragment.get());

	for (int i = 0; i < mDesc.numBindingPoints; i++)
		builder.AddVertexBufferBinding(i, mDesc.vertexStride[min(i, 1)], i == mDesc.instanceDivisorBinding);
	for (auto &attr : mDesc.vertexAttributes)
		builder.AddVertexAttribute(attr.location, attr.binding, VertexFormatToVk(attr.format), attr.offset);

	builder.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
	builder.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR);
	builder.AddDynamicState(VK_DYNAMIC_STATE_DEPTH_BIAS);
	builder.AddDynamicState(VK_DYNAMIC_STATE_STENCIL_REFERENCE);

	// Note: the actual values are ignored since we use dynamic viewport+scissor states
	builder.Viewport(0.0f, 0.0f, 320.0f, 200.0f);
	builder.Scissor(0, 0, 320, 200);

	static const VkPrimitiveTopology vktopology[] = {
		VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
		VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
	};
	static const VkStencilOp op2vk[] = { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_DECREMENT_AND_CLAMP };
	static const VkCompareOp depthfunc2vk[] = { VK_COMPARE_OP_LESS, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_ALWAYS };

	builder.Topology(vktopology[key.DrawType]);
	builder.DepthStencilEnable(key.DepthTest, key.DepthWrite, key.StencilTest);
	builder.DepthFunc(depthfunc2vk[key.DepthFunc]);
	if (fb->device->EnabledFeatures.Features.depthClamp)
		builder.DepthClampEnable(key.DepthClamp);
	builder.DepthBias(key.DepthBias, 0.0f, 0.0f, 0.0f);

	// Same winding convention as the table shaders (see VkRenderPassSetup::CreatePipeline)
	builder.Cull(key.CullMode == Cull_None ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT, key.CullMode == Cull_CW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
	builder.Stencil(VK_STENCIL_OP_KEEP, op2vk[key.StencilPassOp], VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 0xffffffff, 0xffffffff, 0);

	bool independent = fb->device->EnabledFeatures.Features.independentBlend;
	for (int i = 0; i < key.NumColorAttachments; i++)
	{
		VkPipelineColorBlendAttachmentState state = {};
		state.colorWriteMask = (VkColorComponentFlags)key.ColorMask;

		if (key.CustomBlend == 0)
		{
			ColorBlendAttachmentBuilder blendbuilder;
			blendbuilder.ColorWriteMask((VkColorComponentFlags)key.ColorMask);
			BlendMode(blendbuilder, key.RenderStyle);
			state = blendbuilder.Create();
		}
		else
		{
			const auto *att = &key.Att[0];
			if (independent && i < key.NumAtt) att = &key.Att[1 + i];
			state.blendEnable = (key.CustomBlend == 1 && att->blend) ? VK_TRUE : VK_FALSE;
			state.srcColorBlendFactor = BlendFactorToVk(att->srcRGB);
			state.dstColorBlendFactor = BlendFactorToVk(att->dstRGB);
			state.srcAlphaBlendFactor = BlendFactorToVk(att->srcA);
			state.dstAlphaBlendFactor = BlendFactorToVk(att->dstA);
			state.colorBlendOp = BlendEquationToVk(att->eqRGB);
			state.alphaBlendOp = BlendEquationToVk(att->eqA);
		}
		builder.AddColorBlendAttachment(state);
	}

	builder.RasterizationSamples((VkSampleCountFlagBits)key.Samples);
	builder.Layout(mPipelineLayout.get());
	builder.RenderPass(renderPass);
	builder.DebugName(mDesc.name.GetChars());

	item = builder.Create(fb->device.get());
	return item.get();
}

//==========================================================================
//
// VkTexelBuffer
//
//==========================================================================

static VkFormat TexelFormatToVk(int format)
{
	switch (format)
	{
	case VFmt_Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
	case VFmt_Float2: return VK_FORMAT_R32G32_SFLOAT;
	case VFmt_Float: return VK_FORMAT_R32_SFLOAT;
	case VFmt_Byte4: return VK_FORMAT_R8G8B8A8_UNORM;
	case VFmt_Byte4_UInt: return VK_FORMAT_R8G8B8A8_UINT;
	case RPFmt_UInt: return VK_FORMAT_R32_UINT;
	case RPFmt_Int: return VK_FORMAT_R32_SINT;
	case RPFmt_UInt2: return VK_FORMAT_R32G32_UINT;
	case RPFmt_UInt4: return VK_FORMAT_R32G32B32A32_UINT;
	default:
		I_FatalError("CreateTexelBuffer: unsupported format %d", format);
		return VK_FORMAT_UNDEFINED;
	}
}

VkTexelBuffer::VkTexelBuffer(VulkanRenderDevice *fb, int format) : VkHardwareBuffer(fb)
{
	mBufferType = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
	mFormat = TexelFormatToVk(format);
}

VkTexelBuffer::~VkTexelBuffer()
{
	ReleaseView();
}

void VkTexelBuffer::SetData(size_t size, const void *data, BufferUsageType usage)
{
	ReleaseView();
	VkHardwareBuffer::SetData(size, data, usage);
}

void VkTexelBuffer::Resize(size_t newsize)
{
	ReleaseView();
	VkHardwareBuffer::Resize(newsize);
}

void VkTexelBuffer::ReleaseView()
{
	mVersion++;
	if (mView != VK_NULL_HANDLE)
	{
		// the view may still be referenced by recorded commands
		VkDevice device = fb->device->device;
		VkBufferView view = mView;
		fb->GetCommands()->DrawDeleteList->AddGeneric(std::shared_ptr<void>(new char(0), [device, view](void *p) { vkDestroyBufferView(device, view, nullptr); delete (char*)p; }));
		mView = VK_NULL_HANDLE;
	}
}

VkBufferView VkTexelBuffer::GetView()
{
	if (mView == VK_NULL_HANDLE && mBuffer)
	{
		VkBufferViewCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
		info.buffer = mBuffer->buffer;
		info.format = mFormat;
		info.offset = 0;
		info.range = VK_WHOLE_SIZE;
		VkResult result = vkCreateBufferView(fb->device->device, &info, nullptr, &mView);
		CheckVulkanError(result, "Could not create texel buffer view");
	}
	return mView;
}

//==========================================================================
//
// VkTextureArray
//
//==========================================================================

VkTextureArray::VkTextureArray(VulkanRenderDevice *fb, int width, int height, int layers, int numchannels, bool integer, bool mipmap, const char *name)
	: fb(fb), mWidth(width), mHeight(height), mLayers(layers), mChannels(clamp(numchannels, 1, 4)), mInteger(integer), mMipmap(mipmap && !integer)
{
	static const VkFormat normalFormats[] = { VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM };
	static const VkFormat integerFormats[] = { VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_B8G8R8A8_UINT, VK_FORMAT_B8G8R8A8_UINT };
	mFormat = (mInteger ? integerFormats : normalFormats)[mChannels - 1];
	mLevels = mMipmap ? MipLevels(width, height) : 1;

	mImage.Image = ImageBuilder()
		.Format(mFormat)
		.Size(width, height, mLevels, layers)
		.Usage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName(name)
		.Create(fb->device.get());

	mImage.View = ImageViewBuilder()
		.Type(VK_IMAGE_VIEW_TYPE_2D_ARRAY)
		.Image(mImage.Image.get(), mFormat)
		.DebugName(name)
		.Create(fb->device.get());

	TransitionAll(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}

VkTextureArray::~VkTextureArray()
{
	mImage.Reset(fb);
}

void VkTextureArray::TransitionAll(VkImageLayout newLayout)
{
	if (mState == newLayout) return;

	VkAccessFlags srcAccess = 0, dstAccess = 0;
	VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, dstStage = 0;
	switch (mState)
	{
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: srcAccess = VK_ACCESS_TRANSFER_WRITE_BIT; srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT; break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: srcAccess = VK_ACCESS_SHADER_READ_BIT; srcStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; break;
	default: break;
	}
	switch (newLayout)
	{
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: dstAccess = VK_ACCESS_TRANSFER_WRITE_BIT; dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT; break;
	default: dstAccess = VK_ACCESS_SHADER_READ_BIT; dstStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; break;
	}

	PipelineBarrier()
		.AddImage(mImage.Image.get(), mState, newLayout, srcAccess, dstAccess, VK_IMAGE_ASPECT_COLOR_BIT, 0, mLevels, 0, mLayers)
		.Execute(fb->GetCommands()->GetTransferCommands(), srcStage, dstStage);
	mState = newLayout;
	mImage.Layout = newLayout;
}

void VkTextureArray::UploadLayer(int layer, const unsigned char *buffer, int w, int h)
{
	if (layer < 0 || layer >= mLayers || !buffer) return;
	w = min(w, mWidth);
	h = min(h, mHeight);
	if (w <= 0 || h <= 0) return;

	TransitionAll(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	int bytes = mChannels == 3 ? 4 : mChannels;
	size_t totalSize = (size_t)w * h * bytes;
	auto stagingBuffer = BufferBuilder()
		.Size(totalSize)
		.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.DebugName("VkTextureArray.Staging")
		.Create(fb->device.get());

	uint8_t *dst = (uint8_t*)stagingBuffer->Map(0, totalSize);
	if (mChannels == 3)
	{
		// B8G8R8 source data: expand to B8G8R8A8
		for (size_t i = 0; i < (size_t)w * h; i++)
		{
			dst[i * 4 + 0] = buffer[i * 3 + 0];
			dst[i * 4 + 1] = buffer[i * 3 + 1];
			dst[i * 4 + 2] = buffer[i * 3 + 2];
			dst[i * 4 + 3] = 255;
		}
	}
	else
	{
		memcpy(dst, buffer, totalSize);
	}
	stagingBuffer->Unmap();

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = layer;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.width = w;
	region.imageExtent.height = h;
	region.imageExtent.depth = 1;
	fb->GetCommands()->GetTransferCommands()->copyBufferToImage(stagingBuffer->buffer, mImage.Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	// If we queued more than 64 MB of data already: wait until the uploads finish before continuing
	fb->GetCommands()->TransferDeleteList->Add(std::move(stagingBuffer));
	if (fb->GetCommands()->TransferDeleteList->TotalSize > 64 * 1024 * 1024)
		fb->GetCommands()->WaitForCommands(false, true);
}

void VkTextureArray::GenerateMipmaps()
{
	if (mState == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return;
	if (mState != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) TransitionAll(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	auto cmd = fb->GetCommands()->GetTransferCommands();
	if (mLevels <= 1)
	{
		TransitionAll(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		return;
	}

	int mipWidth = mWidth, mipHeight = mHeight;
	for (int i = 1; i < mLevels; i++)
	{
		PipelineBarrier()
			.AddImage(mImage.Image.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, mLayers)
			.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

		int nextWidth = max(mipWidth >> 1, 1);
		int nextHeight = max(mipHeight >> 1, 1);

		VkImageBlit blit = {};
		blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = mLayers;
		blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = mLayers;
		cmd->blitImage(mImage.Image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mImage.Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

		PipelineBarrier()
			.AddImage(mImage.Image.get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, mLayers)
			.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		mipWidth = nextWidth;
		mipHeight = nextHeight;
	}

	PipelineBarrier()
		.AddImage(mImage.Image.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, mLevels - 1, 1, 0, mLayers)
		.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	mState = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	mImage.Layout = mState;
}

VulkanImageView *VkTextureArray::GetView()
{
	if (mState != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) GenerateMipmaps();
	return mImage.View.get();
}

//==========================================================================
//
// VkProgramRenderTarget
//
//==========================================================================

static VkFormat RenderTargetFormatToVk(int fmt)
{
	switch (fmt)
	{
	case RTF_RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
	case RTF_RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
	case RTF_RG16F: return VK_FORMAT_R16G16_SFLOAT;
	case RTF_R8: return VK_FORMAT_R8_UNORM;
	case RTF_R32F: return VK_FORMAT_R32_SFLOAT;
	case RTF_RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
	default: return VK_FORMAT_R32_UINT;
	}
}

VkProgramRenderTarget::VkProgramRenderTarget(VulkanRenderDevice *fb, const RenderTargetDesc &desc) : fb(fb), mDesc(desc)
{
	if (mDesc.colorFormats.Size() == 0 || mDesc.colorFormats.Size() > 8)
		I_FatalError("Render target %s: invalid number of colour attachments (%d)", desc.name.GetChars(), (int)mDesc.colorFormats.Size());
	Create();
}

VkProgramRenderTarget::~VkProgramRenderTarget()
{
	Destroy();
}

void VkProgramRenderTarget::Destroy()
{
	for (auto &c : mColor) c.Reset(fb);
	mColor.clear();
	mDepth.Reset(fb);
	fb->GetCommands()->DrawDeleteList->Add(std::move(mFramebuffer));
	mSharedDepthView = nullptr;
}

void VkProgramRenderTarget::Create()
{
	auto buffers = fb->GetBuffers();
	mWidth = mDesc.width > 0 ? mDesc.width : buffers->GetWidth();
	mHeight = mDesc.height > 0 ? mDesc.height : buffers->GetHeight();

	auto transferCmd = fb->GetCommands()->GetTransferCommands();

	mColor.resize(mDesc.colorFormats.Size());
	mColorFormats.clear();
	for (unsigned i = 0; i < mDesc.colorFormats.Size(); i++)
	{
		VkFormat format = RenderTargetFormatToVk(mDesc.colorFormats[i]);
		mColorFormats.push_back(format);

		VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if (mDesc.colorFormats[i] != RTF_RG16F && mDesc.colorFormats[i] != RTF_R8) usage |= VK_IMAGE_USAGE_STORAGE_BIT;	// storage support is mandatory for these formats

		FString name;
		name.Format("%s[%d]", mDesc.name.GetChars(), i);
		mColor[i].Image = ImageBuilder()
			.Format(format)
			.Size(mWidth, mHeight)
			.Usage(usage)
			.DebugName(name.GetChars())
			.Create(fb->device.get());
		mColor[i].View = ImageViewBuilder()
			.Image(mColor[i].Image.get(), format)
			.DebugName(name.GetChars())
			.Create(fb->device.get());
		mColor[i].AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		mColor[i].Layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkProgramTransitionImage(transferCmd, &mColor[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}

	mHasDepth = false;
	mDepthFormat = buffers->SceneDepthStencilFormat;
	if (mDesc.ownDepth)
	{
		mDepth.Image = ImageBuilder()
			.Format(mDepthFormat)
			.Size(mWidth, mHeight)
			.Usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
			.DebugName(mDesc.name.GetChars())
			.Create(fb->device.get());
		mDepth.View = ImageViewBuilder()
			.Image(mDepth.Image.get(), mDepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
			.DebugName(mDesc.name.GetChars())
			.Create(fb->device.get());
		mDepth.DepthOnlyView = ImageViewBuilder()
			.Image(mDepth.Image.get(), mDepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT)
			.DebugName(mDesc.name.GetChars())
			.Create(fb->device.get());
		mDepth.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		mDepth.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkProgramTransitionImage(transferCmd, &mDepth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		mHasDepth = true;
	}
	else if (mDesc.shareSceneDepth)
	{
		if (buffers->GetSceneSamples() != VK_SAMPLE_COUNT_1_BIT)
		{
			// The multisampled scene depth cannot be paired with single sample colour attachments.
			if (!mWarnedMultisample)
			{
				Printf(TEXTCOLOR_RED "Render target %s: sharing the scene depth buffer is not supported with multisampling; rendering without depth.\n", mDesc.name.GetChars());
				mWarnedMultisample = true;
			}
		}
		else
		{
			mSharedDepthView = buffers->SceneDepthStencil.View.get();
			mHasDepth = mSharedDepthView != nullptr;
		}
	}
}

bool VkProgramRenderTarget::Validate()
{
	auto buffers = fb->GetBuffers();
	bool recreate = false;
	if (mDesc.width <= 0 && mWidth != buffers->GetWidth()) recreate = true;
	if (mDesc.height <= 0 && mHeight != buffers->GetHeight()) recreate = true;
	if (mDesc.shareSceneDepth && !mDesc.ownDepth)
	{
		VulkanImageView *view = buffers->GetSceneSamples() == VK_SAMPLE_COUNT_1_BIT ? buffers->SceneDepthStencil.View.get() : nullptr;
		if (view != mSharedDepthView) recreate = true;
	}
	if (recreate)
	{
		Destroy();
		Create();
	}
	return recreate;
}

std::unique_ptr<VulkanRenderPass> VkProgramRenderTarget::CreateRenderPass(int clearTargets)
{
	RenderPassBuilder builder;
	for (unsigned i = 0; i < mColorFormats.size(); i++)
	{
		builder.AddAttachment(mColorFormats[i], VK_SAMPLE_COUNT_1_BIT,
			(clearTargets & CT_Color) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}
	if (mHasDepth)
	{
		builder.AddDepthStencilAttachment(mDepthFormat, VK_SAMPLE_COUNT_1_BIT,
			(clearTargets & CT_Depth) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			(clearTargets & CT_Stencil) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}
	builder.AddSubpass();
	for (unsigned i = 0; i < mColorFormats.size(); i++)
		builder.AddSubpassColorAttachmentRef(i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	if (mHasDepth)
	{
		builder.AddSubpassDepthStencilAttachmentRef((uint32_t)mColorFormats.size(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		builder.AddExternalSubpassDependency(
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
	}
	else
	{
		builder.AddExternalSubpassDependency(
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
	}
	builder.DebugName("VkProgramRenderTarget.RenderPass");
	return builder.Create(fb->device.get());
}

VulkanRenderPass *VkProgramRenderTarget::GetRenderPass(int clearTargets)
{
	if (!mHasDepth) clearTargets &= ~(CT_Depth | CT_Stencil);
	auto &pass = mRenderPasses[clearTargets & 7];
	if (!pass) pass = CreateRenderPass(clearTargets & 7);
	return pass.get();
}

VulkanFramebuffer *VkProgramRenderTarget::GetFramebuffer()
{
	if (!mFramebuffer)
	{
		FramebufferBuilder builder;
		builder.RenderPass(GetRenderPass(0));
		builder.Size(mWidth, mHeight);
		for (auto &c : mColor) builder.AddAttachment(c.View.get());
		if (mHasDepth) builder.AddAttachment(mDesc.ownDepth ? mDepth.View.get() : mSharedDepthView);
		builder.DebugName("VkProgramRenderTarget.Framebuffer");
		mFramebuffer = builder.Create(fb->device.get());
	}
	return mFramebuffer.get();
}
