/*
** vk_levelmeshrenderer.h
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
** Draws the VkLevelMesh resources into the current scene pass of the
** VkRenderState, through its external-draw seam. Per view: cluster culling
** in compute (frustum, two-phase Hi-Z), then one indirect draw per bucket and
** phase (index compaction) or one indirect command per cluster and bucket
** (draw-indirect-count). The compute work runs between scene render passes;
** the attachments are kept.
*/

#pragma once

// HEADER FILES ------------------------------------------------------------

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "zvulkan/vulkanobjects.h"
#include "hwrenderer/data/hw_levelmeshdata.h"
#include "hw_clock.h"
#include "vulkan/renderer/vk_renderstate.h"

// TYPES -------------------------------------------------------------------

class VulkanRenderDevice;
class VkLevelMesh;

class VkLevelMeshRenderer
{
public:
	VkLevelMeshRenderer(VulkanRenderDevice *fb);
	~VkLevelMeshRenderer();

	// Draws the opaque and masked buckets for the view into the current scene
	// pass. False if nothing was drawn (the caller keeps the classic lists).
	bool Draw(const LevelMeshView &view);

	// Frame boundary: the device has waited for every earlier submit.
	void BeginFrame() { mFrame++; }

	// Latest completed GPU timings and counters (lagged by the frames in flight).
	const LevelMeshPerfStats &GetStats() const { return mStats; }

private:
	struct CullConstants
	{
		float viewProj[16];
		float viewport[4];
		uint32_t phase, cullMode, drawMode, numClusters;
		uint32_t bucketBase[2];
		uint32_t hizLevels;
		uint32_t finalizePhase;
	};

	struct HiZConstants
	{
		int32_t srcOffset[2];
		int32_t srcSize[2];
		int32_t dstSize[2];
		int32_t samples;
		int32_t padding;
	};

	bool CreateObjects();
	bool PrepareLevel(VkLevelMesh *lm, VulkanCommandBuffer *cmd);
	bool PrepareHiZ(int width, int height, VkSampleCountFlagBits samples);
	void UpdateCullSet();
	VulkanPipeline *GetDrawPipeline(bool masked);
	void Cull(VulkanCommandBuffer *cmd, CullConstants &pc, uint32_t phase);
	void DrawPhase(VkLevelMesh *lm, const LevelMeshView &view, int phaseSlot);
	void BuildHiZ(VulkanCommandBuffer *cmd);
	void Timestamp(VulkanCommandBuffer *cmd, int point);
	void Harvest();

	VulkanRenderDevice *fb = nullptr;
	bool mCreated = false, mFailed = false;

	// Compute
	std::unique_ptr<VulkanShader> mCullShader, mHiZShaders[3];
	std::unique_ptr<VulkanDescriptorSetLayout> mCullSetLayout, mHiZSetLayout;
	std::unique_ptr<VulkanPipelineLayout> mCullLayout, mHiZLayout;
	std::unique_ptr<VulkanPipeline> mCullPipeline, mHiZPipelines[3];	// from depth, from MSAA depth, from level
	std::unique_ptr<VulkanDescriptorPool> mPool;
	std::unique_ptr<VulkanSampler> mPointSampler;

	// Draw pipelines, per masked + render target state
	struct PipelineKey
	{
		VulkanRenderPass *renderPass;
		uint64_t bits;
		bool operator<(const PipelineKey &o) const { return renderPass != o.renderPass ? renderPass < o.renderPass : bits < o.bits; }
	};
	std::map<PipelineKey, std::unique_ptr<VulkanPipeline>> mPipelines;
	VkRenderState::ExternalTarget mTarget;	// render-target state of the current Draw

	// Per level
	uint64_t mLevelId = 0;
	unsigned mNumClusters = 0;
	uint32_t mBucketBase[2] = {};
	std::unique_ptr<VulkanBuffer> mVisibility, mCounters, mDrawArgs, mCompact;
	std::unique_ptr<VulkanDescriptorSet> mCullSet;

	// Hi-Z
	VulkanImageView *mDepthView = nullptr;	// scene depth the sets were written for
	int mHiZWidth = 0, mHiZHeight = 0, mHiZLevels = 0;
	VkSampleCountFlagBits mHiZSamples = VK_SAMPLE_COUNT_1_BIT;
	bool mHiZFresh = false;		// image still in UNDEFINED layout
	std::unique_ptr<VulkanImage> mHiZ;
	std::unique_ptr<VulkanImageView> mHiZView;
	std::vector<std::unique_ptr<VulkanImageView>> mHiZLevelViews;
	std::vector<std::unique_ptr<VulkanDescriptorSet>> mHiZSets;

	// Timings + counter readback: a ring of query / readback slots, one per Draw.
	enum { NumSlots = 8, NumPoints = 6, NumCounters = 16 };
	std::unique_ptr<VulkanQueryPool> mQueries;
	std::unique_ptr<VulkanBuffer> mReadback;
	uint32_t *mReadbackData = nullptr;
	struct Slot
	{
		bool pending = false;
		uint64_t serial = 0;
		int cullMode = 0, drawMode = 0;
		uint32_t points = 0;	// written timestamp points (bit mask)
		uint64_t frame = 0;		// mFrame when written
	} mSlots[NumSlots];
	uint64_t mFrame = 0;
	int mSlot = 0;
	uint64_t mSerial = 0, mHarvestedSerial = 0;
	LevelMeshPerfStats mStats;
};
