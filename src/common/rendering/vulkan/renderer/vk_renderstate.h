/*
** vk_renderstate.h
**
** Vulkan backend
**
**---------------------------------------------------------------------------
**
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Copyright 2016-2020 Magnus Norddahl
**
** SPDX-License-Identifier: Zlib
**
**---------------------------------------------------------------------------
**
*/

#pragma once

#include "vulkan/system/vk_hwbuffer.h"
#include "vulkan/shaders/vk_shader.h"
#include "vulkan/renderer/vk_renderpass.h"
#include "vulkan/renderer/vk_streambuffer.h"
#include "vulkan/textures/vk_imagetransition.h"

#include "name.h"

#include "hw_renderstate.h"
#include "hw_material.h"

class VulkanRenderDevice;
class VkRenderPassSetup;
class VkTextureImage;
class VkRenderProgram;
class VkProgramRenderTarget;

class VkRenderState : public FRenderState
{
public:
	VkRenderState(VulkanRenderDevice* fb);
	virtual ~VkRenderState();

	// Draw commands
	void ClearScreen() override;
	void Draw(int dt, int index, int count, bool apply = true) override;
	void DrawIndexed(int dt, int index, int count, bool apply = true) override;

	// Immediate render state change commands. These only change infrequently and should not clutter the render state.
	bool SetDepthClamp(bool on) override;
	void SetDepthMask(bool on) override;
	void SetDepthFunc(int func) override;
	void SetDepthRange(float min, float max) override;
	void SetColorMask(bool r, bool g, bool b, bool a) override;
	void SetStencil(int offs, int op, int flags = -1) override;
	void SetCulling(int mode) override;
	void EnableClipDistance(int num, bool state) override;
	void Clear(int targets) override;
	void EnableStencil(bool on) override;
	void SetScissor(int x, int y, int w, int h) override;
	void SetViewport(int x, int y, int w, int h) override;
	void EnableDepthTest(bool on) override;
	void EnableMultisampling(bool on) override;
	void EnableLineSmooth(bool on) override;
	void EnableDrawBuffers(int count, bool apply) override;

	// Render program mode (hw_renderprogram.h), see vk_renderstate_program.cpp
	void OnSetProgram(IRenderProgram *prog) override;
	void SetRenderTarget(IRenderTarget *target) override;
	void ClearRenderTarget(float r, float g, float b, float a) override;
	void CopySceneColor() override;
	void DrawInstanced(int dt, int index, int count, int instanceCount) override;
	void DrawIndexedInstanced(int dt, int index, int count, int instanceCount) override;
	void DispatchCompute(VkRenderProgram *program, int gx, int gy, int gz);
	void MemoryBarrierCompute();

	void BeginFrame();
	void SetRenderTarget(VkTextureImage *image, VulkanImageView *depthStencilView, int width, int height, VkFormat Format, VkSampleCountFlagBits samples);
	void Bind(int bindingpoint, uint32_t offset);
	void EndRenderPass();
	void EndFrame();

protected:
	void Apply(int dt);
	bool ApplyProgram(int dt);
	void LeaveProgramMode();
	bool PrepareProgramResources(VkRenderProgram *program, bool compute);
	VulkanDescriptorSet *UpdateProgramDescriptorSet(VkRenderProgram *program, std::vector<uint32_t> &dynamicOffsets, bool compute);
	bool WriteProgramUniforms(const void *data, size_t size, uint32_t &offset);
	int ProgramTargetWidth() const;
	int ProgramTargetHeight() const;
	void BeginProgramTargetRenderPass(VulkanCommandBuffer *cmdbuffer);
	void ApplyRenderPass(int dt);
	void ApplyStencilRef();
	void ApplyDepthBias();
	void ApplyScissor();
	void ApplyViewport();
	void ApplyStreamData();
	void ApplyMatrices();
	void ApplyPushConstants();
	void FillPushConstants();
	VkRenderPassSetup *GetRenderPassSetup();
	void ApplyHWBufferSet();
	void ApplyVertexBuffers();
	void ApplyMaterial();

	void BeginRenderPass(VulkanCommandBuffer *cmdbuffer);
	void WaitForStreamBuffers();

	VulkanRenderDevice* fb = nullptr;

	bool mDepthClamp = true;
	VulkanCommandBuffer *mCommandBuffer = nullptr;
	VkPipelineKey mPipelineKey = {};
	VkRenderPassSetup *mPassSetup = nullptr;
	int mClearTargets = 0;
	bool mNeedApply = true;

	int mScissorX = 0, mScissorY = 0, mScissorWidth = -1, mScissorHeight = -1;
	int mViewportX = 0, mViewportY = 0, mViewportWidth = -1, mViewportHeight = -1;
	float mViewportDepthMin = 0.0f, mViewportDepthMax = 1.0f;
	bool mScissorChanged = true;
	bool mViewportChanged = true;

	bool mDepthTest = false;
	bool mDepthWrite = false;
	bool mStencilTest = false;

	bool mStencilRefChanged = false;
	int mStencilRef = 0;
	int mStencilOp = 0;
	int mDepthFunc = 0;
	int mColorMask = 15;
	int mCullMode = 0;

	PushConstants mPushConstants = {};

	uint32_t mLastViewpointOffset = 0xffffffff;
	uint32_t mLastMatricesOffset = 0xffffffff;
	uint32_t mLastStreamDataOffset = 0xffffffff;
	uint32_t mViewpointOffset = 0;

	VkStreamBufferWriter mStreamBufferWriter;
	VkMatrixBufferWriter mMatrixBufferWriter;

	int mLastVertexOffsets[2] = { 0, 0 };
	IVertexBuffer *mLastVertexBuffer = nullptr;
	IIndexBuffer *mLastIndexBuffer = nullptr;

	bool mLastModelMatrixEnabled = true;
	bool mLastTextureMatrixEnabled = true;

	int mApplyCount = 0;

	// Render program mode
	bool mInProgramMode = false;
	VkProgramRenderTarget *mUserTarget = nullptr;
	float mClearColor[4] = { 0, 0, 0, 0 };
	bool mClearColorCustom = false;

	std::unique_ptr<VulkanBuffer> mProgramUniformRing;
	size_t mProgramUniformRingPos = 0;
	size_t mProgramUniformRingSize = 0;
	uint8_t *mProgramUniformRingMap = nullptr;

	std::unique_ptr<VulkanDescriptorPool> mProgramDescriptorPool;
	int mProgramDescriptorSetsLeft = 0;
	std::unique_ptr<VulkanBuffer> mProgramNullTexelBuffer;
	std::unique_ptr<IHardwareTexture> mProgramNullArray;	// view of array samplers that have no texture bound
	VkBufferView mProgramNullTexelView = VK_NULL_HANDLE;
	VkProgramRenderTarget *mProgramUserTargetLast = nullptr;

	VulkanDescriptorSet *mProgramSet = nullptr;		// last set bound by a draw (owned by the delete list)
	std::vector<uintptr_t> mProgramSetSignature;
	VkRenderProgram *mProgramSetProgram = nullptr;
	int mProgramSetGeneration = -1;
	VulkanPipeline *mLastProgramPipeline = nullptr;
	IVertexBuffer *mProgramLastVB = nullptr, *mProgramLastInstanceVB = nullptr;
	int mProgramLastVBOffset = -1, mProgramLastInstanceOffset = -1;

	VkTextureImage mSceneCopy;
	int mSceneCopyWidth = 0, mSceneCopyHeight = 0;
	bool mSceneDepthSampled = false;

	struct RenderTarget
	{
		VkTextureImage *Image = nullptr;
		VulkanImageView *DepthStencil = nullptr;
		int Width = 0;
		int Height = 0;
		VkFormat Format = VK_FORMAT_R16G16B16A16_SFLOAT;
		VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;
		int DrawBuffers = 1;
	} mRenderTarget;
};

class VkRenderStateMolten : public VkRenderState
{
public:
	using VkRenderState::VkRenderState;

	void Draw(int dt, int index, int count, bool apply = true) override;
};
