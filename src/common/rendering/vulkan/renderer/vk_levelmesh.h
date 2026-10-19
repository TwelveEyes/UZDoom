/*
** vk_levelmesh.h
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
** Owns the device-local levelmesh buffers, the per-frame staging ring for
** dirty uploads, the bindless texture table and the LevelMeshSet descriptor
** set / pipeline layouts. Everything is additive: the classic layouts and
** descriptor sets are untouched. Nothing is created on the device until a
** level uploads a levelmesh.
*/

#pragma once

// HEADER FILES ------------------------------------------------------------

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "zvulkan/vulkanobjects.h"
#include "zstring.h"
#include "hwrenderer/data/buffers.h"
#include "hwrenderer/data/hw_levelmeshdata.h"

// TYPES -------------------------------------------------------------------

class VulkanRenderDevice;
class VkMaterial;
class FGameTexture;

// LevelMeshSet bindings (set LM_SET_INDEX of the levelmesh pipeline layout).
enum ELevelMeshBinding
{
	LMBIND_VERTICES,
	LMBIND_INDICES,
	LMBIND_SURFACES,
	LMBIND_CLUSTERS,
	LMBIND_CLUSTERSECTORS,
	LMBIND_CLUSTERSURFACES,
	LMBIND_SECTORSTATE,
	LMBIND_SIDESTATE,
	LMBIND_PLANEXFORM,
	LMBIND_TEXTURETABLE,
	LMBIND_RESOLVED,		// per surface: LevelMeshResolved of the current view (resolve.comp)
	LMBIND_SHADING,			// LevelMeshShading of the current view
	LMBIND_LIGHTS3D,		// LevelMeshLight3D
	LMBIND_POLYOBJS,		// LevelMeshPolyobj
	LMBIND_SKYSOURCES,		// per sky source: its sky configuration this frame (uint)
	LMBIND_LEAFMASK,		// bit per leaf: the leaves the view may show (LevelMeshView::leafMask, ticket 18)
	LMBIND_EXPOSURE,		// per view index (0 main, 1 + k portal view k): leaf bits, then line bits (mark pass, ticket 18)
	LMBIND_NUMBUFFERS,
	LMBIND_SAMPLERS = LMBIND_NUMBUFFERS,	// sampler[NUMSAMPLERS], indexed by clamp mode
};

class VkLevelMesh
{
public:
	// Pipeline layout: 0 fixed, 1 HW buffers (classic), 2 LevelMeshSet, 3 bindless textures.
	enum { LM_SET_INDEX = 2, LM_TEXTURE_SET_INDEX = 3 };

	VkLevelMesh(VulkanRenderDevice *fb);
	~VkLevelMesh();

	// GLSL constants mirroring hw_levelmeshdata.h / ELevelMeshBinding for the
	// levelmesh shaders, with the LevelMeshSet at lmSet.
	static FString ShaderDefines(int lmSet, int textureSet);

	bool Supported(bool report);
	// Subgroup basic / ballot / arithmetic operations in fragment shaders (the mark pass dedupes its atomics).
	bool FragmentSubgroups() const { return mFragmentSubgroups; }
	void SetData(const LevelMeshUploadData *data);
	bool IsResident(uint64_t id) const { return mResident && mId == id; }
	void PrintStats();

	// Start of frame (no command buffer pending): sampler rebinding,
	// texture resolves, bindless descriptor writes, translation table upload.
	void BeginFrame();
	void MaterialInvalidated(VkMaterial *mat);
	// Texture swaps: true when the id's table entry is resolved, otherwise
	// the id joins the level's ids and is resolved at the next BeginFrame.
	bool TextureReady(int id);

	// Dirty state stream: copies the records into the staging ring and
	// records one scatter dispatch into the transfer commands, ahead of
	// every draw of the frame. Returns the staging bytes used.
	size_t UpdateState(const LevelMeshStateUpdate &update);
	void SamplersChanged() { mSamplersDirty = true; }

	// Staging ring (HW_MAX_PIPELINE_BUFFERS slots, host-visible, usable as a
	// storage buffer by compute scatters). Allocations are valid for the frame.
	struct UploadAlloc
	{
		VulkanBuffer *buffer = nullptr;
		size_t offset = 0;
		void *data = nullptr;
	};
	UploadAlloc AllocUpload(size_t size, size_t align = 16);
	void QueueCopy(const UploadAlloc &src, VulkanBuffer *dst, size_t dstOffset, size_t size);
	void FlushUploads(VulkanCommandBuffer *cmd);	// records the queued copies + a barrier

	VulkanBuffer *GetVertexBuffer() { return mBuffers[LMBIND_VERTICES].get(); }
	VulkanBuffer *GetIndexBuffer() { return mBuffers[LMBIND_INDICES].get(); }
	VulkanBuffer *GetBuffer(ELevelMeshBinding b) { return mBuffers[b].get(); }
	VulkanDescriptorSetLayout *GetSetLayout() { return mSetLayout.get(); }
	VulkanDescriptorSetLayout *GetTextureSetLayout() { return mTextureSetLayout.get(); }
	VulkanDescriptorSet *GetSet() { return mSet.get(); }
	VulkanDescriptorSet *GetTextureSet() { return mTextureSet.get(); }
	VulkanPipelineLayout *GetPipelineLayout() { return mPipelineLayout.get(); }
	unsigned GetClusterCount() const { return mCounts[LMBIND_CLUSTERS]; }
	unsigned GetSkySourceCount() const { return mCounts[LMBIND_SKYSOURCES]; }
	unsigned GetIndexCount() const { return mCounts[LMBIND_INDICES]; }
	unsigned GetBucketIndexCount(int bucket) const { return mBucketIndices[bucket]; }
	// Ticket 18: words of a leaf bitset, and of one view's exposure region
	// (leaf bits, then line bits) in LMBIND_EXPOSURE.
	unsigned GetLeafWords() const { return mLeafWords; }
	unsigned GetLineWords() const { return mLineWords; }
	unsigned GetExposureWords() const { return mLeafWords + mLineWords; }
	enum { MaxExposureViews = 1 + 64 };	// the main view + VkLevelMeshRenderer::MaxPortalViews
	uint64_t GetId() const { return mResident ? mId : 0; }

private:
	void CreateDeviceObjects();
	void ReleaseLevel();
	void WriteSamplers();
	LevelMeshTextureEntry Resolve(int id);
	uint32_t AllocSlots(uint32_t count);
	void FreeSlots(uint32_t first, uint32_t count);
	void WriteSlot(uint32_t slot, VkImageView view, VkImageLayout layout);
	void FlushSlotWrites();
	void UploadTable();
	bool CreateScatter();

	VulkanRenderDevice *fb = nullptr;

	// Capability
	std::vector<std::string> mMissing;		// required features the device lacks
	std::string mReported;					// last reported missing list
	uint32_t mBindlessCapacity = 0;
	bool mFragmentSubgroups = false;

	// Device objects (created on first upload)
	bool mCreated = false;
	std::unique_ptr<VulkanDescriptorSetLayout> mSetLayout;
	std::unique_ptr<VulkanDescriptorSetLayout> mTextureSetLayout;
	std::unique_ptr<VulkanDescriptorPool> mSetPool;
	std::unique_ptr<VulkanDescriptorPool> mTexturePool;
	std::unique_ptr<VulkanDescriptorSet> mTextureSet;
	std::unique_ptr<VulkanPipelineLayout> mPipelineLayout;

	// Level resources
	bool mResident = false;
	uint64_t mId = 0;
	std::unique_ptr<VulkanBuffer> mBuffers[LMBIND_NUMBUFFERS];
	size_t mSizes[LMBIND_NUMBUFFERS] = {};
	unsigned mCounts[LMBIND_NUMBUFFERS] = {};
	unsigned mBucketIndices[LMB_COUNT] = {};	// per bucket, over all clusters
	unsigned mLeafWords = 0, mLineWords = 0;
	std::unique_ptr<VulkanDescriptorSet> mSet;
	bool mSamplersDirty = false;
	double mUploadMs = 0;

	// Bindless slots: one contiguous block per material (layer i = slot + i).
	struct MaterialSlots
	{
		uint32_t first = 0, count = 0;
		std::vector<int> ids;		// translation table entries pointing here
	};
	std::unordered_map<VkMaterial *, MaterialSlots> mMaterials;
	std::vector<std::vector<uint32_t>> mFreeBlocks;	// by block size
	uint32_t mNextSlot = 1;			// slot 0 = null texture
	uint32_t mSlotsUsed = 1;
	uint32_t mSlotOverflows = 0;
	struct SlotWrite
	{
		uint32_t slot;
		VkImageView view;
		VkImageLayout layout;
	};
	std::vector<SlotWrite> mSlotWrites;

	// Translation table (CPU mirror)
	std::vector<LevelMeshTextureEntry> mTable;
	std::vector<FGameTexture *> mFrame;	// per id: the animation frame the entry was resolved from
	std::vector<int> mUsedIds;			// ids the level references
	std::vector<uint8_t> mUsedFlag;
	std::vector<uint8_t> mPendingFlag;
	std::vector<int> mPending;
	uint32_t mTableDirtyMin = ~0u, mTableDirtyMax = 0;
	unsigned mResolveCalls = 0;

	// State scatter. Keep in sync with the push constant block of scatter.comp.
	struct ScatterConstants
	{
		uint32_t sectorIndices, sectorRecords, numSectors;	// word offsets into the staging buffer
		uint32_t clusterEntries, numClusters;
		uint32_t sideIndices, sideRecords, numSides;
		uint32_t xformIndices, xformRecords, numXforms;
		uint32_t light3DIndices, light3DRecords, numLights3D;
		uint32_t polyIndices, polyRecords, numPolys;
		uint32_t padding[3];
	};
	static_assert(sizeof(ScatterConstants) == 20 * 4, "scatter.comp declares 20 words of push constants");
	bool mScatterCreated = false, mScatterFailed = false;
	std::unique_ptr<VulkanShader> mScatterShader;
	std::unique_ptr<VulkanDescriptorSetLayout> mScatterSetLayout;
	std::unique_ptr<VulkanPipelineLayout> mScatterLayout;
	std::unique_ptr<VulkanPipeline> mScatterPipeline;
	std::unique_ptr<VulkanDescriptorPool> mScatterPool;

	// Staging ring
	struct RingSlot
	{
		std::unique_ptr<VulkanBuffer> buffer;
		uint8_t *data = nullptr;
		size_t used = 0;
		std::unique_ptr<VulkanDescriptorSet> scatterSet;	// the buffer as the scatter source
		VulkanBuffer *scatterSetBuffer = nullptr;
	};
	RingSlot mRing[HW_MAX_PIPELINE_BUFFERS];
	int mRingIndex = 0;
	struct PendingCopy
	{
		VulkanBuffer *src, *dst;
		VkBufferCopy region;
	};
	std::vector<PendingCopy> mCopies;
};
