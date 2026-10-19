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

	// Draws the sky surfaces of a sky configuration (LevelMeshView::
	// skySources) that the last Draw's culls found visible, with the render
	// state's current depth / stencil / color mask (an HWSkyPortal's stencil
	// geometry). False if there is no Draw of this frame to take them from.
	bool DrawSky(uint32_t config);

	// Frame boundary: the device has waited for every earlier submit. Makes
	// the previous frame's visibility readback host-visible.
	void BeginFrame();

	// The cluster visibility bitset (bit = cluster index) the view wrote in
	// the previous rendered frame, or null: no levelmesh draw of that view last
	// frame, a different level, or the frame was not rendered. Valid until the
	// next BeginFrame.
	const uint32_t *GetVisibility(uint64_t levelId, uint64_t viewKey, unsigned &words);

	// Latest completed GPU timings and counters (lagged by the frames in flight).
	const LevelMeshPerfStats &GetStats() const { return mStats; }

private:
	struct CullConstants
	{
		float viewProj[16];
		float viewport[4];
		uint32_t phase, cullMode, drawMode, numClusters;
		uint32_t hizLevels;
		uint32_t finalizePhase;
		uint32_t padding[2];
	};

	// Keep in sync with the push constant block of resolve.comp.
	struct ResolveConstants
	{
		uint32_t numClusters;
		uint32_t padding[3];
	};
	static_assert(sizeof(ResolveConstants) == 4 * 4, "resolve.comp declares 4 words of push constants");

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
	bool DrawPortalView(const LevelMeshView &view);
	bool BeginFrameSetup(VkLevelMesh *lm, const VkRenderState::ExternalTarget &ext);
	bool PrepareHiZ(int width, int height, VkSampleCountFlagBits samples);
	struct ViewBuffers;
	void UpdateCullSet(ViewBuffers &v);
	ViewBuffers *PortalBuffers(VulkanCommandBuffer *cmd, unsigned &index);
	void CopyVisibility(VulkanCommandBuffer *cmd, unsigned viewIndex, uint64_t viewKey);
	VulkanPipeline *GetDrawPipeline(bool masked, int program, const VkRenderState::ExternalTarget &target);
	void DrawSkyBucket(VkLevelMesh *lm, int phaseSlot, uint32_t config, const VkRenderState::ExternalTarget &target);
	void UploadSkySources(VkLevelMesh *lm, VulkanCommandBuffer *cmd, const LevelMeshView &view);
	void Cull(VulkanCommandBuffer *cmd, CullConstants &pc, uint32_t phase);
	void Resolve(VulkanCommandBuffer *cmd);
	void UploadShading(VkLevelMesh *lm, VulkanCommandBuffer *cmd, const LevelMeshShading &shading);
	void DrawPhase(VkLevelMesh *lm, const LevelMeshView &view, int phaseSlot);
	void BuildHiZ(VulkanCommandBuffer *cmd);
	void Timestamp(VulkanCommandBuffer *cmd, int point);
	void Harvest();

	VulkanRenderDevice *fb = nullptr;
	bool mCreated = false, mFailed = false;

	// Compute
	std::unique_ptr<VulkanShader> mCullShader, mResolveShader, mHiZShaders[3];
	std::unique_ptr<VulkanDescriptorSetLayout> mCullSetLayout, mHiZSetLayout;
	std::unique_ptr<VulkanPipelineLayout> mCullLayout, mResolveLayout, mHiZLayout;
	std::unique_ptr<VulkanPipeline> mCullPipeline, mResolvePipeline, mHiZPipelines[3];	// from depth, from MSAA depth, from level
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
	uint32_t mBucketBase[LMB_DRAWN] = {};	// compacted index region per drawn bucket
	uint32_t mBucketCount[LMB_DRAWN] = {};	// indices per drawn bucket over all clusters (0 = not drawn)
	// Per view: the main view's (mMain), and one set per portal view of a
	// frame (ticket 15; created on demand, per level). Portal views draw
	// with per-cluster commands, so they have no compacted index buffer.
	struct ViewBuffers
	{
		std::unique_ptr<VulkanBuffer> visibility, counters, drawArgs, compact;
		std::unique_ptr<VulkanBuffer> drawn;	// per cluster: drawn by the last cull phase (resolve.comp input)
		std::unique_ptr<VulkanBuffer> visBits;	// bit per cluster: visible in the last cull phase (copied to mVisReadback)
		std::unique_ptr<VulkanDescriptorSet> cullSet;
	};
	ViewBuffers mMain;
	std::vector<std::unique_ptr<ViewBuffers>> mPortals;
	ViewBuffers *mView = &mMain;	// the view of the current Draw
	unsigned mPortalViews = 0;		// portal views drawn this frame
	bool mMirrored = false;			// the current Draw's view is mirrored
	std::unique_ptr<LevelMeshShading> mShading;	// last uploaded shading block (null = none for this level)
	VulkanBuffer *mShadingBuffer = nullptr;		// the LMBIND_SHADING buffer it went into
	std::vector<uint32_t> mSkySources;			// last uploaded LMBIND_SKYSOURCES contents
	VulkanBuffer *mSkySourceBuffer = nullptr;
	// The last Draw: its frame and modes (DrawSky reuses its draw arguments).
	uint64_t mDrawFrame = ~0ull;
	uint64_t mSetupFrame = ~0ull;	// BeginFrameSetup ran in this frame
	int mDrawCullMode = 0, mDrawDrawMode = 0;

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
	enum { NumSlots = 8, NumPoints = 8, NumCounters = 6 * LMB_DRAWN + 4 };	// LMC_COUNT
	enum { MaxPortalViews = 64 };	// per frame; further portal views stay classic
	std::unique_ptr<VulkanQueryPool> mQueries;
	// Per level: the views' visibility bitsets, per slot (1 + MaxPortalViews)
	// views of mVisWords words; view 0 = the main view, 1 + k = portal view k.
	std::unique_ptr<VulkanBuffer> mVisReadback;
	uint32_t *mVisReadbackData = nullptr;
	unsigned mVisWords = 0;
	int mReadSlot = -1;	// the slot of the previous frame's views (BeginFrame), -1 = none
	std::unique_ptr<VulkanBuffer> mReadback;
	uint32_t *mReadbackData = nullptr;
	struct Slot
	{
		bool pending = false;
		uint64_t serial = 0;
		int cullMode = 0, drawMode = 0;
		uint32_t points = 0;	// written timestamp points (bit mask)
		uint64_t frame = 0;		// mFrame when written
		unsigned portalViews = 0;	// portal views timed in mPortalQueries
		uint64_t levelId = 0;		// the level of the visibility bitsets
		unsigned viewCount = 0;		// views with a bitset (viewKeys[0 .. viewCount), 0 = none written)
		uint64_t viewKeys[1 + MaxPortalViews] = {};	// LevelMeshView::viewKey per bitset
	} mSlots[NumSlots];
	std::unique_ptr<VulkanQueryPool> mPortalQueries;	// per slot: start / end per portal view
	uint64_t mFrame = 0;
	int mSlot = 0;
	uint64_t mSerial = 0, mHarvestedSerial = 0;
	LevelMeshPerfStats mStats;
};
