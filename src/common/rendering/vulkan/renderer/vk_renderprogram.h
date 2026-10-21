/*
** vk_renderprogram.h
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

#pragma once

#include "hw_renderprogram.h"
#include "hw_ihwtexture.h"
#include "zvulkan/vulkanobjects.h"
#include "zvulkan/vulkanbuilders.h"
#include "vulkan/system/vk_hwbuffer.h"
#include "vulkan/textures/vk_imagetransition.h"
#include <map>
#include <memory>
#include <vector>

class VulkanRenderDevice;

// Everything a program pipeline depends on besides the program itself.
// Compared with memcmp: always zero the key before filling it.
struct VkProgramPipelineKey
{
	uint64_t RenderPass;
	int DrawType;
	int DepthTest, DepthWrite, DepthFunc, DepthClamp, DepthBias;
	int StencilTest, StencilPassOp;
	int ColorMask, CullMode;
	int Samples, NumColorAttachments;
	int CustomBlend;	// 0 = blend from RenderStyle, 1 = custom blend, 2 = blending disabled
	FRenderStyle RenderStyle;
	// blend state, ints only so memcmp is safe: [0] = base state, [1..3] = per attachment overrides
	struct BlendAtt { int blend, srcRGB, dstRGB, srcA, dstA, eqRGB, eqA; } Att[4];
	int NumAtt;

	bool operator<(const VkProgramPipelineKey &other) const { return memcmp(this, &other, sizeof(VkProgramPipelineKey)) < 0; }
};

class VkRenderProgram : public IRenderProgram
{
public:
	VkRenderProgram(VulkanRenderDevice *fb, const RenderProgramDesc &desc);
	~VkRenderProgram();

	bool IsValid() const override { return true; }
	const RenderProgramDesc &Desc() const override { return mDesc; }

	VulkanDescriptorSetLayout *GetSetLayout() { return mSetLayout.get(); }
	VulkanPipelineLayout *GetPipelineLayout() { return mPipelineLayout.get(); }
	VulkanPipeline *GetGraphicsPipeline(const VkProgramPipelineKey &key, VulkanRenderPass *renderPass);
	VulkanPipeline *GetComputePipeline();

	// indices into Desc().bindings ordered by binding number (the order of dynamic offsets)
	const std::vector<int> &SortedBindings() const { return mSortedBindings; }
	static VkDescriptorType DescriptorTypeFor(const RenderProgramBinding &binding);

private:
	std::unique_ptr<VulkanShader> CompileStage(ShaderType type, const FString &source);

	VulkanRenderDevice *fb = nullptr;
	RenderProgramDesc mDesc;
	std::unique_ptr<VulkanShader> mVertex, mFragment, mCompute;
	std::unique_ptr<VulkanDescriptorSetLayout> mSetLayout;
	std::unique_ptr<VulkanDescriptorSetLayout> mEmptyLayout;
	std::unique_ptr<VulkanPipelineLayout> mPipelineLayout;
	std::map<VkProgramPipelineKey, std::unique_ptr<VulkanPipeline>> mPipelines;
	std::unique_ptr<VulkanPipeline> mComputePipeline;
	std::vector<int> mSortedBindings;
};

// samplerBuffer / usamplerBuffer / isamplerBuffer backing store (uniform texel buffer)
class VkTexelBuffer : public IDataBuffer, public VkHardwareBuffer
{
public:
	VkTexelBuffer(VulkanRenderDevice *fb, int format);
	~VkTexelBuffer();

	void BindRange(FRenderState *state, size_t start, size_t length) override {}
	void SetData(size_t size, const void *data, BufferUsageType usage) override;
	void Resize(size_t newsize) override;

	// Changes whenever the view (buffer) is replaced.
	uint32_t Version() const { return mVersion; }

	// A view of the current buffer; created lazily and replaced whenever the buffer is recreated.
	VkBufferView GetView();
	VulkanBuffer *GetBuffer() { return mBuffer.get(); }

private:
	void ReleaseView();

	VkFormat mFormat;
	VkBufferView mView = VK_NULL_HANDLE;
	uint32_t mVersion = 1;
};

// 2D array texture created with DFrameBuffer::CreateTextureArray
class VkTextureArray : public IHardwareTexture
{
public:
	VkTextureArray(VulkanRenderDevice *fb, int width, int height, int layers, int numchannels, bool integer, bool mipmap, const char *name);
	~VkTextureArray();

	void AllocateBuffer(int w, int h, int texelsize) override {}
	uint8_t *MapBuffer() override { return nullptr; }
	unsigned int CreateTexture(unsigned char *buffer, int w, int h, int texunit, bool mipmap, const char *name) override { return 0; }
	void UploadLayer(int layer, const unsigned char *buffer, int w, int h) override;
	void GenerateMipmaps() override;

	// Makes sure the image is in the sampled layout (finishes pending uploads).
	VulkanImageView *GetView();

private:
	void TransitionAll(VkImageLayout newLayout);

	VulkanRenderDevice *fb = nullptr;
	VkTextureImage mImage;
	VkFormat mFormat;
	int mWidth, mHeight, mLayers, mChannels, mLevels;
	bool mInteger, mMipmap;
	VkImageLayout mState = VK_IMAGE_LAYOUT_UNDEFINED;	// layout of all subresources
};

// Offscreen colour attachments (+ shared scene depth or own depth)
class VkProgramRenderTarget : public IRenderTarget
{
public:
	VkProgramRenderTarget(VulkanRenderDevice *fb, const RenderTargetDesc &desc);
	~VkProgramRenderTarget();

	int Width() const override { return mWidth; }
	int Height() const override { return mHeight; }
	const RenderTargetDesc &Desc() const override { return mDesc; }

	// Recreates images/framebuffer when the scene size or the shared depth changed. Returns true if recreated.
	bool Validate();

	int NumColor() const { return (int)mDesc.colorFormats.Size(); }
	VkTextureImage &Color(int i) { return mColor[i]; }
	VkTextureImage &Depth() { return mDepth; }
	bool HasDepth() const { return mHasDepth; }
	bool OwnsDepth() const { return mDesc.ownDepth; }
	VkFormat ColorFormat(int i) const { return mColorFormats[i]; }
	static bool IsInteger(int rtFormat) { return rtFormat == RTF_R32UI; }

	VulkanRenderPass *GetRenderPass(int clearTargets);
	VulkanFramebuffer *GetFramebuffer();
	VkFormat DepthFormat() const { return mDepthFormat; }

private:
	void Create();
	void Destroy();
	std::unique_ptr<VulkanRenderPass> CreateRenderPass(int clearTargets);

	VulkanRenderDevice *fb = nullptr;
	RenderTargetDesc mDesc;
	int mWidth = 0, mHeight = 0;
	std::vector<VkTextureImage> mColor;
	std::vector<VkFormat> mColorFormats;
	VkTextureImage mDepth;		// own depth image (ownDepth only)
	VkFormat mDepthFormat = VK_FORMAT_UNDEFINED;
	bool mHasDepth = false;
	VulkanImageView *mSharedDepthView = nullptr;
	std::unique_ptr<VulkanRenderPass> mRenderPasses[8];
	std::unique_ptr<VulkanFramebuffer> mFramebuffer;
	bool mWarnedMultisample = false;
};

// Generic layout transition used for program render targets (conservative barriers).
void VkProgramTransitionImage(VulkanCommandBuffer *cmd, VkTextureImage *image, VkImageLayout newLayout);
