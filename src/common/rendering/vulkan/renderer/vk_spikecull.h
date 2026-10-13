/*
** vk_spikecull.h
**
** Ticket 05 spike (throwaway, never merged): GPU cluster cull (frustum +
** two-phase Hi-Z) writing vkCmdDrawIndexedIndirectCount commands.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#pragma once

#include <memory>
#include <vector>
#include "zvulkan/vulkanobjects.h"
#include "hw_spike.h"

class VulkanRenderDevice;

class VkSpikeCull
{
public:
	VkSpikeCull(VulkanRenderDevice *fb);
	~VkSpikeCull();

	bool Supported() const { return mSupported; }
	void SetMesh(const SpikeMeshData *data);
	void UpdateClusters(const SpikeClusterUpdate *updates, unsigned count);
	void Render(const SpikeView &view);
	void SceneTimestamp(bool begin);

private:
	void CreateShaders();
	void EnsurePipeline();
	bool EnsureHiz(int vpw, int vph);
	void UpdateCullDescriptors();
	void BeginFrameStats();
	void Timestamp(VulkanCommandBuffer *cmd, int index);
	void DrawIndirect(VulkanBuffer *cmds, uint32_t countOffset, const SpikeView &view);
	void Cull(VulkanCommandBuffer *cmd, const SpikeView &view, int phase, bool useHiz);
	void BuildHiz(VulkanCommandBuffer *cmd, int vpx, int vpy);
	void DestroyAll();
	void DumpHiz(VulkanCommandBuffer *cmd);

	VulkanRenderDevice *fb = nullptr;
	bool mSupported = false;

	// Mesh
	std::unique_ptr<VulkanBuffer> mVertexBuffer, mIndexBuffer, mClusterBuffer, mVisBuffer;
	std::unique_ptr<VulkanBuffer> mCmd1, mCmd2, mCounters, mNoCullCmd, mReadback;
	uint32_t mClusterCount = 0;
	uint32_t mIndexCount = 0;

	// Dirty cluster uploads
	std::unique_ptr<VulkanBuffer> mStaging[2];
	size_t mStagingCapacity = 0;
	int mStagingIndex = 0;
	uint32_t mPendingCount = 0;
	std::unique_ptr<VulkanDescriptorSet> mScatterSets[2];
	VulkanBuffer *mScatterSetsCluster = nullptr;

	// Shaders + pipelines
	std::unique_ptr<VulkanShader> mDrawVS, mDrawFS, mCullCS, mHiz0CS, mHiz0MSCS, mHizNCS, mScatterCS;
	std::unique_ptr<VulkanDescriptorSetLayout> mScatterSetLayout;
	std::unique_ptr<VulkanPipelineLayout> mScatterLayout;
	std::unique_ptr<VulkanPipeline> mScatterPipeline;
	std::unique_ptr<VulkanPipelineLayout> mDrawLayout;
	std::unique_ptr<VulkanPipeline> mDrawPipeline;
	VulkanRenderPass *mDrawPipelinePass = nullptr;
	int mDrawPipelineBuffers = 0;
	int mDrawPipelineCull = -1;
	VkSampleCountFlagBits mDrawPipelineSamples = VK_SAMPLE_COUNT_1_BIT;

	std::unique_ptr<VulkanDescriptorSetLayout> mCullSetLayout, mHiz0SetLayout, mHizNSetLayout;
	std::unique_ptr<VulkanPipelineLayout> mCullLayout, mHiz0Layout, mHizNLayout;
	std::unique_ptr<VulkanPipeline> mCullPipeline, mHiz0Pipeline, mHiz0MSPipeline, mHizNPipeline;
	std::unique_ptr<VulkanDescriptorPool> mPool;
	std::unique_ptr<VulkanDescriptorSet> mCullSet;
	std::vector<std::unique_ptr<VulkanDescriptorSet>> mHizSets;
	std::unique_ptr<VulkanSampler> mSampler;

	// Hi-Z
	std::unique_ptr<VulkanImage> mHiz;
	std::unique_ptr<VulkanImageView> mHizView;
	std::vector<std::unique_ptr<VulkanImageView>> mHizMipViews;
	int mHizW = 0, mHizH = 0, mHizMips = 0;
	int mHizVpW = 0, mHizVpH = 0;
	VulkanImageView *mHizDepthView = nullptr;
	int mHizDepthSamples = 1;
	bool mCullSetDirty = true;

	// Stats
	std::unique_ptr<VulkanQueryPool> mQueries;
	enum { QueriesPerSlot = 8 };
	struct Slot { int mode = -1; uint32_t written = 0; bool counters = false; } mSlots[2];
	uint64_t mFrame = 0;
	uint64_t mStatsFrame = ~0ull;
	int mCurSlot = 0;
};
