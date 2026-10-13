/*
** vk_spikecull.cpp
**
** Ticket 05 spike (throwaway, never merged): GPU cluster cull (frustum +
** two-phase Hi-Z) writing vkCmdDrawIndexedIndirectCount commands, drawn with a
** flat per-surface color.
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

#include "vk_spikecull.h"
#include "zvulkan/vulkanbuilders.h"
#include "vulkan/system/vk_renderdevice.h"
#include "vulkan/system/vk_commandbuffer.h"
#include "vulkan/renderer/vk_renderstate.h"
#include "vulkan/textures/vk_renderbuffers.h"
#include "printf.h"
#include "c_cvars.h"

CVAR(Int, r_spike_cull, 1, 0)	// 0 no backface cull, 1 cull with front face CW, 2 with front face CCW
CVAR(Int, r_spike_fsload, 0, 0)	// synthetic fragment ALU iterations (shading-cost stand-in)
CVAR(Int, r_spike_debug, 0, 0)	// debug bits: 1 flip the Hi-Z y mapping, 2 never reject by Hi-Z

//==========================================================================
//
// Shaders
//
//==========================================================================

static const char *DrawVS = R"(
#version 450
layout(location = 0) in vec3 aPos;
layout(location = 1) in uint aSurf;
layout(push_constant) uniform PC { mat4 mvp; } pc;
layout(location = 0) flat out uint vSurf;
void main()
{
	gl_Position = pc.mvp * vec4(aPos, 1.0);
	gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
	vSurf = aSurf;
}
)";

static const char *DrawFS = R"(
#version 450
layout(location = 0) flat in uint vSurf;
layout(location = 0) out vec4 FragColor;
layout(push_constant) uniform PC { layout(offset = 64) int fsload; } pc;
uint hash(uint x)
{
	x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
	return x;
}
void main()
{
	uint h = hash(vSurf);
	vec3 c = vec3(float(h & 255u), float((h >> 8) & 255u), float((h >> 16) & 255u)) / 255.0;
	// Debug knob: synthetic per-fragment ALU work, a stand-in for real shading cost.
	vec3 acc = vec3(0.0);
	for (int i = 0; i < pc.fsload; i++)
		acc = fract(acc * 1.37 + c * 0.11 + gl_FragCoord.xyx * 0.0001);
	c = mix(c, acc, 0.001);
	FragColor = vec4(c * 0.7 + 0.15, 1.0);
}
)";

// Phase 1: clusters visible last frame that pass the frustum -> cmd1.
// Phase 2: all clusters, frustum (+ Hi-Z) -> visibility; visible and not drawn
// in phase 1 -> cmd2. Counters: 0 count1, 1 count2, 2 tris1, 3 tris2,
// 4 frustum pass, 5 Hi-Z rejected.
static const char *CullCS = R"(
#version 450
layout(local_size_x = 64) in;
struct Cluster { float minx, miny, minz; uint first; float maxx, maxy, maxz; uint count; };
layout(std430, binding = 0) readonly buffer Clusters { Cluster clusters[]; };
layout(std430, binding = 1) buffer Vis { uint vis[]; };
layout(std430, binding = 2) writeonly buffer Cmd1 { uint cmd1[]; };
layout(std430, binding = 3) writeonly buffer Cmd2 { uint cmd2[]; };
layout(std430, binding = 4) buffer Counters { uint counters[]; };
layout(binding = 5) uniform sampler2D hiz;
layout(push_constant) uniform PC
{
	mat4 mvp;
	vec4 vp;	// viewport w, h
	uint count;
	uint phase;
	uint hizMips;
	uint useHiz;
	uint flipY;
} pc;

void main()
{
	uint i = gl_GlobalInvocationID.x;
	if (i >= pc.count)
		return;
	Cluster c = clusters[i];

	uint andMask = 31u;
	bool behind = false;
	vec2 mn = vec2(1e30), mx = vec2(-1e30);
	float minZ = 1.0;
	for (int k = 0; k < 8; k++)
	{
		vec3 p = vec3((k & 1) != 0 ? c.maxx : c.minx, (k & 2) != 0 ? c.maxy : c.miny, (k & 4) != 0 ? c.maxz : c.minz);
		vec4 v = pc.mvp * vec4(p, 1.0);
		v.z = (v.z + v.w) * 0.5;
		uint m = 0u;
		if (v.x < -v.w) m |= 1u;
		if (v.x > v.w) m |= 2u;
		if (v.y < -v.w) m |= 4u;
		if (v.y > v.w) m |= 8u;
		if (v.z < 0.0) m |= 16u;
		andMask &= m;
		if (v.w <= 1e-4)
		{
			behind = true;
		}
		else
		{
			vec3 n = v.xyz / v.w;
			mn = min(mn, n.xy);
			mx = max(mx, n.xy);
			minZ = min(minZ, n.z);
		}
	}
	bool inFrustum = (andMask == 0u);

	if (pc.phase == 1u)
	{
		if ((vis[i] & 1u) != 0u && inFrustum)
		{
			uint slot = atomicAdd(counters[0], 1u);
			cmd1[slot * 5u + 0u] = c.count;
			cmd1[slot * 5u + 1u] = 1u;
			cmd1[slot * 5u + 2u] = c.first;
			cmd1[slot * 5u + 3u] = 0u;
			cmd1[slot * 5u + 4u] = 0u;
			atomicAdd(counters[2], c.count / 3u);
			vis[i] |= 2u;
		}
		else
		{
			vis[i] &= ~2u;
		}
		return;
	}

	bool visible = inFrustum;
	if (visible)
		atomicAdd(counters[4], 1u);
	if (visible && pc.useHiz != 0u && !behind)
	{
		mn = clamp(mn, vec2(-1.0), vec2(1.0));
		mx = clamp(mx, vec2(-1.0), vec2(1.0));
		if ((pc.flipY & 1u) != 0u)
		{
			float t = mn.y;
			mn.y = -mx.y;
			mx.y = -t;
		}
		vec2 smin = (mn * 0.5 + 0.5) * pc.vp.xy * 0.5;	// in Hi-Z mip 0 texels
		vec2 smax = (mx * 0.5 + 0.5) * pc.vp.xy * 0.5;
		vec2 size = smax - smin;
		int L = int(ceil(log2(max(max(size.x, size.y), 1.0))));
		L = clamp(L, 0, int(pc.hizMips) - 1);
		if ((pc.flipY & 4u) != 0u)
			L = int(pc.hizMips) - 1;
		ivec2 dim = textureSize(hiz, L);
		float scale = exp2(-float(L));
		ivec2 t0 = clamp(ivec2(floor(smin * scale)), ivec2(0), dim - 1);
		ivec2 t1 = clamp(ivec2(floor(smax * scale)), ivec2(0), dim - 1);
		float d = 0.0;
		for (int y = t0.y; y <= t1.y; y++)
			for (int x = t0.x; x <= t1.x; x++)
				d = max(d, texelFetch(hiz, ivec2(x, y), L).r);
		if (minZ > d && (pc.flipY & 2u) == 0u)
		{
			visible = false;
			atomicAdd(counters[5], 1u);
		}
	}

	bool drawn1 = pc.useHiz != 0u && (vis[i] & 2u) != 0u;
	vis[i] = visible ? 1u : 0u;
	if (visible && !drawn1)
	{
		uint slot = atomicAdd(counters[1], 1u);
		cmd2[slot * 5u + 0u] = c.count;
		cmd2[slot * 5u + 1u] = 1u;
		cmd2[slot * 5u + 2u] = c.first;
		cmd2[slot * 5u + 3u] = 0u;
		cmd2[slot * 5u + 4u] = 0u;
		atomicAdd(counters[3], c.count / 3u);
	}
}
)";

// Dirty cluster records: scatter from the host-visible update list (one dispatch,
// independent of how many clusters a moving plane touches).
static const char *ScatterCS = R"(
#version 450
layout(local_size_x = 64) in;
struct Cluster { float minx, miny, minz; uint first; float maxx, maxy, maxz; uint count; };
struct Update { uint index; Cluster c; };
layout(std430, binding = 0) writeonly buffer Clusters { Cluster clusters[]; };
layout(std430, binding = 1) readonly buffer Updates { Update updates[]; };
layout(push_constant) uniform PC { uint count; } pc;
void main()
{
	uint i = gl_GlobalInvocationID.x;
	if (i < pc.count)
		clusters[updates[i].index] = updates[i].c;
}
)";

// Hi-Z mip 0: max depth over each 2x2 block of the viewport's depth.
static const char *Hiz0CS = R"(
#version 450
layout(local_size_x = 8, local_size_y = 8) in;
#ifdef MSAA
layout(binding = 0) uniform sampler2DMS depthTex;
#else
layout(binding = 0) uniform sampler2D depthTex;
#endif
layout(binding = 1, r32f) writeonly uniform image2D dst;
layout(push_constant) uniform PC { ivec2 srcOffset; ivec2 srcSize; ivec2 dstSize; int samples; int pad; } pc;
void main()
{
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (p.x >= pc.dstSize.x || p.y >= pc.dstSize.y)
		return;
	float d = 0.0;
	for (int y = 0; y < 2; y++)
	{
		for (int x = 0; x < 2; x++)
		{
			ivec2 q = min(p * 2 + ivec2(x, y), pc.srcSize - 1) + pc.srcOffset;
#ifdef MSAA
			for (int s = 0; s < pc.samples; s++)
				d = max(d, texelFetch(depthTex, q, s).r);
#else
			d = max(d, texelFetch(depthTex, q, 0).r);
#endif
		}
	}
	imageStore(dst, p, vec4(d));
}
)";

static const char *HizNCS = R"(
#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 0, r32f) readonly uniform image2D src;
layout(binding = 1, r32f) writeonly uniform image2D dst;
layout(push_constant) uniform PC { ivec2 srcOffset; ivec2 srcSize; ivec2 dstSize; int samples; int pad; } pc;
void main()
{
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (p.x >= pc.dstSize.x || p.y >= pc.dstSize.y)
		return;
	// Vulkan mip sizes round down: the last row/column also takes the odd source texel.
	ivec2 s0 = p * 2;
	ivec2 s1 = min(s0 + 1, pc.srcSize - 1);
	if (p.x == pc.dstSize.x - 1) s1.x = pc.srcSize.x - 1;
	if (p.y == pc.dstSize.y - 1) s1.y = pc.srcSize.y - 1;
	float d = 0.0;
	for (int y = s0.y; y <= s1.y; y++)
		for (int x = s0.x; x <= s1.x; x++)
			d = max(d, imageLoad(src, ivec2(x, y)).r);
	imageStore(dst, p, vec4(d));
}
)";

struct CullPush
{
	float mvp[16];
	float vp[4];
	uint32_t count;
	uint32_t phase;
	uint32_t hizMips;
	uint32_t useHiz;
	uint32_t flipY;
};

struct HizPush
{
	int32_t srcOffset[2];
	int32_t srcSize[2];
	int32_t dstSize[2];
	int32_t samples;
	int32_t pad;
};

//==========================================================================
//
//
//
//==========================================================================

VkSpikeCull::VkSpikeCull(VulkanRenderDevice *fb) : fb(fb)
{
	mSupported = fb->device->SupportsExtension(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME) && vkCmdDrawIndexedIndirectCountKHR != nullptr;
	Printf("Spike05: VK_KHR_draw_indirect_count %s\n", mSupported ? "available" : "MISSING (spike disabled)");
	if (!mSupported)
		return;

	CreateShaders();

	mQueries = QueryPoolBuilder()
		.QueryType(VK_QUERY_TYPE_TIMESTAMP, QueriesPerSlot * 2)
		.DebugName("Spike.Queries")
		.Create(fb->device.get());
	fb->GetCommands()->GetTransferCommands()->resetQueryPool(mQueries.get(), 0, QueriesPerSlot * 2);

	mCounters = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
		.Size(64)
		.DebugName("Spike.Counters")
		.Create(fb->device.get());

	mReadback = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU)
		.Size(2 * 64)
		.DebugName("Spike.Readback")
		.Create(fb->device.get());

	mSampler = SamplerBuilder()
		.MinFilter(VK_FILTER_NEAREST)
		.MagFilter(VK_FILTER_NEAREST)
		.MipmapMode(VK_SAMPLER_MIPMAP_MODE_NEAREST)
		.AddressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
		.MaxLod(16.0f)
		.DebugName("Spike.Sampler")
		.Create(fb->device.get());

	mPool = DescriptorPoolBuilder()
		.MaxSets(1 + 20 + 2)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 + 4)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2)
		.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * 20)
		.Flags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
		.DebugName("Spike.Pool")
		.Create(fb->device.get());
}

VkSpikeCull::~VkSpikeCull()
{
	DestroyAll();
}

void VkSpikeCull::DestroyAll()
{
	mHizSets.clear();
	mCullSet.reset();
	for (auto &set : mScatterSets)
		set.reset();
}

void VkSpikeCull::CreateShaders()
{
	auto dev = fb->device.get();
	mDrawVS = ShaderBuilder().Type(ShaderType::Vertex).AddSource("spike_draw.vp", DrawVS).DebugName("Spike.DrawVS").Create("spike_draw.vp", dev);
	mDrawFS = ShaderBuilder().Type(ShaderType::Fragment).AddSource("spike_draw.fp", DrawFS).DebugName("Spike.DrawFS").Create("spike_draw.fp", dev);
	mCullCS = ShaderBuilder().Type(ShaderType::Compute).AddSource("spike_cull.cp", CullCS).DebugName("Spike.CullCS").Create("spike_cull.cp", dev);
	mHiz0CS = ShaderBuilder().Type(ShaderType::Compute).AddSource("spike_hiz0.cp", Hiz0CS).DebugName("Spike.Hiz0CS").Create("spike_hiz0.cp", dev);
	std::string ms = Hiz0CS;
	ms.insert(ms.find('\n', 1) + 1, "#define MSAA\n");
	mHiz0MSCS = ShaderBuilder().Type(ShaderType::Compute).AddSource("spike_hiz0ms.cp", ms).DebugName("Spike.Hiz0MSCS").Create("spike_hiz0ms.cp", dev);
	mScatterCS = ShaderBuilder().Type(ShaderType::Compute).AddSource("spike_scatter.cp", ScatterCS).DebugName("Spike.ScatterCS").Create("spike_scatter.cp", dev);
	mScatterSetLayout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.DebugName("Spike.ScatterSetLayout")
		.Create(dev);
	mScatterLayout = PipelineLayoutBuilder()
		.AddSetLayout(mScatterSetLayout.get())
		.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, 4)
		.DebugName("Spike.ScatterLayout")
		.Create(dev);
	mScatterPipeline = ComputePipelineBuilder().Layout(mScatterLayout.get()).ComputeShader(mScatterCS.get()).DebugName("Spike.ScatterPipeline").Create(dev);
	mHizNCS = ShaderBuilder().Type(ShaderType::Compute).AddSource("spike_hizn.cp", HizNCS).DebugName("Spike.HizNCS").Create("spike_hizn.cp", dev);

	mDrawLayout = PipelineLayoutBuilder()
		.AddPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, 64)
		.AddPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 64, 4)
		.DebugName("Spike.DrawLayout")
		.Create(dev);

	mCullSetLayout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.DebugName("Spike.CullSetLayout")
		.Create(dev);
	mCullLayout = PipelineLayoutBuilder()
		.AddSetLayout(mCullSetLayout.get())
		.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullPush))
		.DebugName("Spike.CullLayout")
		.Create(dev);
	mCullPipeline = ComputePipelineBuilder().Layout(mCullLayout.get()).ComputeShader(mCullCS.get()).DebugName("Spike.CullPipeline").Create(dev);

	mHiz0SetLayout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.DebugName("Spike.Hiz0SetLayout")
		.Create(dev);
	mHiz0Layout = PipelineLayoutBuilder()
		.AddSetLayout(mHiz0SetLayout.get())
		.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HizPush))
		.DebugName("Spike.Hiz0Layout")
		.Create(dev);
	mHiz0Pipeline = ComputePipelineBuilder().Layout(mHiz0Layout.get()).ComputeShader(mHiz0CS.get()).DebugName("Spike.Hiz0Pipeline").Create(dev);
	mHiz0MSPipeline = ComputePipelineBuilder().Layout(mHiz0Layout.get()).ComputeShader(mHiz0MSCS.get()).DebugName("Spike.Hiz0MSPipeline").Create(dev);

	mHizNSetLayout = DescriptorSetLayoutBuilder()
		.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
		.DebugName("Spike.HizNSetLayout")
		.Create(dev);
	mHizNLayout = PipelineLayoutBuilder()
		.AddSetLayout(mHizNSetLayout.get())
		.AddPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HizPush))
		.DebugName("Spike.HizNLayout")
		.Create(dev);
	mHizNPipeline = ComputePipelineBuilder().Layout(mHizNLayout.get()).ComputeShader(mHizNCS.get()).DebugName("Spike.HizNPipeline").Create(dev);
}

//==========================================================================
//
// Mesh upload (load time)
//
//==========================================================================

void VkSpikeCull::SetMesh(const SpikeMeshData *data)
{
	if (!mSupported)
		return;

	vkDeviceWaitIdle(fb->device->device);
	mCullSet.reset();
	mCullSetDirty = true;
	mPendingCount = 0;
	for (auto &set : mScatterSets)
		set.reset();
	mScatterSetsCluster = nullptr;

	mClusterCount = data->Clusters.Size();
	mIndexCount = data->Indices.Size();
	if (mClusterCount == 0 || mIndexCount == 0)
	{
		mClusterCount = 0;
		return;
	}

	size_t vsize = data->Vertices.Size() * sizeof(SpikeVertex);
	size_t isize = data->Indices.Size() * sizeof(uint32_t);
	size_t csize = data->Clusters.Size() * sizeof(SpikeCluster);
	size_t cmdsize = (size_t)mClusterCount * 20;
	auto dev = fb->device.get();

	mVertexBuffer = BufferBuilder().Usage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).Size(vsize).DebugName("Spike.Vertices").Create(dev);
	mIndexBuffer = BufferBuilder().Usage(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).Size(isize).DebugName("Spike.Indices").Create(dev);
	mClusterBuffer = BufferBuilder().Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).Size(csize).DebugName("Spike.Clusters").Create(dev);
	mVisBuffer = BufferBuilder().Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).Size(mClusterCount * 4).DebugName("Spike.Vis").Create(dev);
	mCmd1 = BufferBuilder().Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT).Size(cmdsize).DebugName("Spike.Cmd1").Create(dev);
	mCmd2 = BufferBuilder().Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT).Size(cmdsize).DebugName("Spike.Cmd2").Create(dev);
	mNoCullCmd = BufferBuilder().Usage(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).Size(20).DebugName("Spike.NoCullCmd").Create(dev);

	uint32_t nocull[5] = { mIndexCount, 1, 0, 0, 0 };
	auto cmd = fb->GetCommands()->GetTransferCommands();
	auto staging = BufferTransfer()
		.AddBuffer(mVertexBuffer.get(), data->Vertices.Data(), vsize)
		.AddBuffer(mIndexBuffer.get(), data->Indices.Data(), isize)
		.AddBuffer(mClusterBuffer.get(), data->Clusters.Data(), csize)
		.AddBuffer(mNoCullCmd.get(), nocull, sizeof(nocull))
		.Execute(dev, cmd);
	cmd->fillBuffer(mVisBuffer->buffer, 0, VK_WHOLE_SIZE, 0);
	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	fb->GetCommands()->TransferDeleteList->Add(std::move(staging));

	Printf("Spike05: mesh %u verts, %u tris, %u clusters (budget %d), %.1f MB\n", data->Vertices.Size(), mIndexCount / 3, mClusterCount, data->Budget,
		(vsize + isize + csize + mClusterCount * 4 + cmdsize * 2) / (1024.0 * 1024.0));
}

//==========================================================================
//
// Dirty cluster records (moving planes)
//
//==========================================================================

void VkSpikeCull::UpdateClusters(const SpikeClusterUpdate *updates, unsigned count)
{
	if (!mSupported || mClusterCount == 0 || count == 0)
		return;

	static_assert(sizeof(SpikeClusterUpdate) == 36, "update record must match the scatter shader");
	size_t need = (size_t)count * sizeof(SpikeClusterUpdate);
	if (need > mStagingCapacity)
	{
		vkDeviceWaitIdle(fb->device->device);
		mStagingCapacity = std::max(need * 2, (size_t)64 * 1024);
		for (int i = 0; i < 2; i++)
		{
			mScatterSets[i].reset();
			mStaging[i] = BufferBuilder()
				.Usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)
				.Size(mStagingCapacity)
				.DebugName("Spike.Updates")
				.Create(fb->device.get());
		}
		mScatterSetsCluster = nullptr;
	}
	if (mScatterSetsCluster != mClusterBuffer.get())
	{
		for (int i = 0; i < 2; i++)
		{
			mScatterSets[i] = mPool->allocate(mScatterSetLayout.get());
			WriteDescriptors()
				.AddBuffer(mScatterSets[i].get(), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mClusterBuffer.get())
				.AddBuffer(mScatterSets[i].get(), 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mStaging[i].get())
				.Execute(fb->device.get());
		}
		mScatterSetsCluster = mClusterBuffer.get();
	}
	mStagingIndex ^= 1;
	memcpy(mStaging[mStagingIndex]->Map(0, need), updates, need);
	mStaging[mStagingIndex]->Unmap();
	mPendingCount = count;
}

//==========================================================================
//
// Stats: timestamps + counters, read one frame late
//
//==========================================================================

void VkSpikeCull::BeginFrameStats()
{
	mFrame++;
	int prev = (int)((mFrame - 1) & 1);
	mCurSlot = (int)(mFrame & 1);

	Slot &ps = mSlots[prev];
	if (ps.mode >= 0)
	{
		uint64_t ts[QueriesPerSlot] = {};
		double period = fb->device->PhysicalDevice.Properties.Properties.limits.timestampPeriod;
		for (int i = 0; i < QueriesPerSlot; i++)
		{
			if (ps.written & (1u << i))
				mQueries->getResults(prev * QueriesPerSlot + i, 1, sizeof(uint64_t), &ts[i], sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
		}
		auto ms = [&](int a, int b) { return ((ps.written >> a) & (ps.written >> b) & 1) ? (double)(ts[b] - ts[a]) * period / 1e6 : 0.0; };

		spikestats.mode = ps.mode;
		spikestats.clusters = mClusterCount;
		spikestats.gCull1 = spikestats.gDraw1 = spikestats.gHiz = spikestats.gCull2 = spikestats.gDraw2 = 0;
		spikestats.gScene = ms(0, 5);
		if (ps.mode == SPIKE_HIZ)
		{
			spikestats.gCull1 = ms(0, 1);
			spikestats.gDraw1 = ms(1, 2);
			spikestats.gHiz = ms(2, 3);
			spikestats.gCull2 = ms(3, 4);
			spikestats.gDraw2 = ms(4, 5);
		}
		else if (ps.mode == SPIKE_FRUSTUM)
		{
			spikestats.gCull2 = ms(0, 4);
			spikestats.gDraw2 = ms(4, 5);
		}
		else if (ps.mode == SPIKE_NOCULL)
		{
			spikestats.gDraw2 = ms(0, 5);
		}

		if (ps.counters)
		{
			auto c = (const uint32_t *)mReadback->Map(prev * 64, 64);
			spikestats.drawn1 = c[0];
			spikestats.drawn2 = c[1];
			spikestats.tris = (int64_t)c[2] + c[3];
			spikestats.frustum = c[4];
			if ((r_spike_debug & 16) && (mFrame % 50) == 0)
				Printf("Spike05 dbg: c1=%u c2=%u frustum=%u hizrej=%u\n", c[0], c[1], c[4], c[5]);
			mReadback->Unmap();
		}
		else if (ps.mode == SPIKE_NOCULL)
		{
			spikestats.drawn1 = 0;
			spikestats.drawn2 = mClusterCount;
			spikestats.tris = mIndexCount / 3;
			spikestats.frustum = 0;
		}
		else
		{
			spikestats.drawn1 = spikestats.drawn2 = spikestats.frustum = 0;
			spikestats.tris = 0;
		}
	}

	Slot &cs = mSlots[mCurSlot];
	if (cs.written)
		fb->GetCommands()->GetTransferCommands()->resetQueryPool(mQueries.get(), mCurSlot * QueriesPerSlot, QueriesPerSlot);
	cs.mode = -1;
	cs.written = 0;
	cs.counters = false;
}

void VkSpikeCull::Timestamp(VulkanCommandBuffer *cmd, int index)
{
	cmd->writeTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mQueries.get(), mCurSlot * QueriesPerSlot + index);
	mSlots[mCurSlot].written |= 1u << index;
}

void VkSpikeCull::SceneTimestamp(bool begin)
{
	if (!mSupported)
		return;
	if (begin)
	{
		BeginFrameStats();
		mSlots[mCurSlot].mode = SPIKE_OFF;
	}
	Timestamp(fb->GetCommands()->GetDrawCommands(), begin ? 0 : 5);
}

//==========================================================================
//
// Pipelines and descriptors
//
//==========================================================================

void VkSpikeCull::EnsurePipeline()
{
	auto rs = fb->GetRenderState();
	VulkanRenderPass *pass = rs->SpikeGetRenderPass();
	int buffers = rs->SpikeGetDrawBuffers();
	VkSampleCountFlagBits samples = rs->SpikeGetSamples();
	if (mDrawPipeline && mDrawPipelinePass == pass && mDrawPipelineBuffers == buffers && mDrawPipelineSamples == samples && mDrawPipelineCull == r_spike_cull)
		return;
	mDrawPipelineCull = r_spike_cull;

	if (mDrawPipeline)
		Printf("Spike05: recreating draw pipeline\n");

	GraphicsPipelineBuilder builder;
	builder.Cache(fb->GetRenderPassManager()->GetCache());
	builder.AddVertexShader(mDrawVS.get());
	builder.AddFragmentShader(mDrawFS.get());
	builder.AddVertexBufferBinding(0, sizeof(SpikeVertex));
	builder.AddVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
	builder.AddVertexAttribute(1, 0, VK_FORMAT_R32_UINT, 12);
	builder.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
	builder.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR);
	builder.Viewport(0.0f, 0.0f, 320.0f, 200.0f);
	builder.Scissor(0, 0, 320, 200);
	builder.Topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	builder.DepthStencilEnable(true, true, false);
	builder.DepthFunc(VK_COMPARE_OP_LESS);
	if (fb->device->EnabledFeatures.Features.depthClamp)
		builder.DepthClampEnable(true);
	builder.Cull(mDrawPipelineCull == 0 ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT, mDrawPipelineCull == 2 ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
	ColorBlendAttachmentBuilder blend;
	blend.ColorWriteMask(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT);
	builder.AddColorBlendAttachment(blend.Create());
	ColorBlendAttachmentBuilder noblend;
	noblend.ColorWriteMask(0);
	for (int i = 1; i < buffers; i++)
		builder.AddColorBlendAttachment(noblend.Create());
	builder.RasterizationSamples(samples);
	builder.Layout(mDrawLayout.get());
	builder.RenderPass(pass);
	builder.DebugName("Spike.DrawPipeline");
	mDrawPipeline = builder.Create(fb->device.get());
	mDrawPipelinePass = pass;
	mDrawPipelineBuffers = buffers;
	mDrawPipelineSamples = samples;
}

bool VkSpikeCull::EnsureHiz(int vpw, int vph)
{
	auto buffers = fb->GetBuffers();
	VulkanImageView *depthView = buffers->SceneDepthStencil.DepthOnlyView.get();
	int samples = (int)buffers->GetSceneSamples();
	if (mHiz && mHizVpW == vpw && mHizVpH == vph && mHizDepthView == depthView && mHizDepthSamples == samples)
		return true;

	vkDeviceWaitIdle(fb->device->device);
	mHizSets.clear();
	mHizMipViews.clear();
	mHizView.reset();
	mHiz.reset();
	mCullSet.reset();
	mCullSetDirty = true;

	auto dev = fb->device.get();
	mHizVpW = vpw;
	mHizVpH = vph;
	mHizDepthView = depthView;
	mHizDepthSamples = samples;
	mHizW = std::max((vpw + 1) / 2, 1);
	mHizH = std::max((vph + 1) / 2, 1);
	mHizMips = 1;
	for (int w = mHizW, h = mHizH; w > 1 || h > 1; mHizMips++)
	{
		w = std::max(w / 2, 1);
		h = std::max(h / 2, 1);
	}
	if (mHizMips > 20)
		mHizMips = 20;

	mHiz = ImageBuilder()
		.Size(mHizW, mHizH, mHizMips)
		.Format(VK_FORMAT_R32_SFLOAT)
		.Usage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName("Spike.Hiz")
		.Create(dev);
	mHizView = ImageViewBuilder()
		.Image(mHiz.get(), VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, mHizMips, 1)
		.DebugName("Spike.HizView")
		.Create(dev);
	for (int i = 0; i < mHizMips; i++)
	{
		mHizMipViews.push_back(ImageViewBuilder()
			.Image(mHiz.get(), VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1, 1)
			.DebugName("Spike.HizMipView")
			.Create(dev));
	}

	PipelineBarrier()
		.AddImage(mHiz.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, mHizMips)
		.Execute(fb->GetCommands()->GetDrawCommands(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	WriteDescriptors write;
	for (int i = 0; i < mHizMips; i++)
	{
		auto set = mPool->allocate(i == 0 ? mHiz0SetLayout.get() : mHizNSetLayout.get());
		if (i == 0)
			write.AddCombinedImageSampler(set.get(), 0, depthView, mSampler.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		else
			write.AddStorageImage(set.get(), 0, mHizMipViews[i - 1].get(), VK_IMAGE_LAYOUT_GENERAL);
		write.AddStorageImage(set.get(), 1, mHizMipViews[i].get(), VK_IMAGE_LAYOUT_GENERAL);
		mHizSets.push_back(std::move(set));
	}
	write.Execute(dev);
	Printf("Spike05: Hi-Z %dx%d, %d mips, depth samples %d\n", mHizW, mHizH, mHizMips, samples);
	return true;
}

void VkSpikeCull::UpdateCullDescriptors()
{
	if (!mCullSetDirty && mCullSet)
		return;
	mCullSet = mPool->allocate(mCullSetLayout.get());
	WriteDescriptors()
		.AddBuffer(mCullSet.get(), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mClusterBuffer.get())
		.AddBuffer(mCullSet.get(), 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mVisBuffer.get())
		.AddBuffer(mCullSet.get(), 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mCmd1.get())
		.AddBuffer(mCullSet.get(), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mCmd2.get())
		.AddBuffer(mCullSet.get(), 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mCounters.get())
		.AddCombinedImageSampler(mCullSet.get(), 5, mHizView.get(), mSampler.get(), VK_IMAGE_LAYOUT_GENERAL)
		.Execute(fb->device.get());
	mCullSetDirty = false;
}

//==========================================================================
//
// Frame
//
//==========================================================================

void VkSpikeCull::Cull(VulkanCommandBuffer *cmd, const SpikeView &view, int phase, bool useHiz)
{
	CullPush push;
	memcpy(push.mvp, view.mvp, sizeof(push.mvp));
	push.vp[0] = (float)mHizVpW;
	push.vp[1] = (float)mHizVpH;
	push.vp[2] = (float)mHizW;
	push.vp[3] = (float)mHizH;
	push.count = mClusterCount;
	push.phase = phase;
	push.hizMips = mHizMips;
	push.useHiz = useHiz ? 1 : 0;
	push.flipY = r_spike_debug;
	cmd->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, mCullPipeline.get());
	cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, mCullLayout.get(), 0, mCullSet.get());
	cmd->pushConstants(mCullLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
	cmd->dispatch((mClusterCount + 63) / 64, 1, 1);
	PipelineBarrier()
		.AddMemory(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
}

void VkSpikeCull::DrawIndirect(VulkanBuffer *cmds, uint32_t countOffset, const SpikeView &view)
{
	auto rs = fb->GetRenderState();
	auto cmd = rs->SpikeBeginPass();
	EnsurePipeline();
	cmd->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, mDrawPipeline.get());
	VkBuffer vb = mVertexBuffer->buffer;
	VkDeviceSize offset = 0;
	cmd->bindVertexBuffers(0, 1, &vb, &offset);
	cmd->bindIndexBuffer(mIndexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
	cmd->pushConstants(mDrawLayout.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, 64, view.mvp);
	int32_t fsload = r_spike_fsload;
	cmd->pushConstants(mDrawLayout.get(), VK_SHADER_STAGE_FRAGMENT_BIT, 64, 4, &fsload);
	if (cmds)
		vkCmdDrawIndexedIndirectCountKHR(cmd->buffer, cmds->buffer, 0, mCounters->buffer, countOffset, mClusterCount, 20);
	else
		cmd->drawIndexedIndirect(mNoCullCmd->buffer, 0, 1, 20);
	rs->EndRenderPass();
}

void VkSpikeCull::BuildHiz(VulkanCommandBuffer *cmd, int vpx, int vpy)
{
	auto depth = &fb->GetBuffers()->SceneDepthStencil;
	// The scene render pass leaves depth in DEPTH_STENCIL_ATTACHMENT_OPTIMAL; the
	// tracked VkTextureImage::Layout is not updated by render passes.
	static bool once = false;
	if (!once)
	{
		once = true;
		Printf("Spike05: tracked depth layout %d\n", (int)depth->Layout);
	}
	VkImageLayout oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	PipelineBarrier()
		.AddImage(depth->Image.get(), oldLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, depth->AspectMask)
		.Execute(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	int sw = mHizVpW, sh = mHizVpH;
	for (int i = 0; i < mHizMips; i++)
	{
		int dw = i == 0 ? mHizW : std::max(sw / 2, 1);
		int dh = i == 0 ? mHizH : std::max(sh / 2, 1);
		HizPush push;
		push.srcOffset[0] = i == 0 ? vpx : 0;
		push.srcOffset[1] = i == 0 ? vpy : 0;
		push.srcSize[0] = sw;
		push.srcSize[1] = sh;
		push.dstSize[0] = dw;
		push.dstSize[1] = dh;
		push.samples = mHizDepthSamples;
		push.pad = 0;
		VulkanPipelineLayout *layout = i == 0 ? mHiz0Layout.get() : mHizNLayout.get();
		VulkanPipeline *pipeline = i == 0 ? (mHizDepthSamples > 1 ? mHiz0MSPipeline.get() : mHiz0Pipeline.get()) : mHizNPipeline.get();
		cmd->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, mHizSets[i].get());
		cmd->pushConstants(layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
		cmd->dispatch((dw + 7) / 8, (dh + 7) / 8, 1);
		if ((r_spike_debug & 8) && (mFrame % 50) == 0)
			Printf("Spike05 hiz build %d: src %dx%d dst %dx%d groups %dx%d\n", i, sw, sh, dw, dh, (dw + 7) / 8, (dh + 7) / 8);
		PipelineBarrier()
			.AddMemory(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
			.Execute(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		sw = dw;
		sh = dh;
	}

	PipelineBarrier()
		.AddImage(depth->Image.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, oldLayout,
			VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, depth->AspectMask)
		.Execute(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
}

void VkSpikeCull::DumpHiz(VulkanCommandBuffer *cmd)
{
	// Debug: copy every mip to host memory, then wait for the GPU and print each mip's max/min.
	size_t total = 0;
	std::vector<size_t> offs;
	for (int i = 0, w = mHizW, h = mHizH; i < mHizMips; i++, w = std::max(w / 2, 1), h = std::max(h / 2, 1))
	{
		offs.push_back(total);
		total += (size_t)w * h * 4;
	}
	auto buf = BufferBuilder().Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU).Size(total).Create(fb->device.get());
	PipelineBarrier().AddMemory(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT).Execute(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	std::vector<VkBufferImageCopy> regions;
	for (int i = 0, w = mHizW, h = mHizH; i < mHizMips; i++, w = std::max(w / 2, 1), h = std::max(h / 2, 1))
	{
		VkBufferImageCopy r = {};
		r.bufferOffset = offs[i];
		r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		r.imageSubresource.mipLevel = i;
		r.imageSubresource.layerCount = 1;
		r.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
		regions.push_back(r);
	}
	vkCmdCopyImageToBuffer(cmd->buffer, mHiz->image, VK_IMAGE_LAYOUT_GENERAL, buf->buffer, (uint32_t)regions.size(), regions.data());
	PipelineBarrier().AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT).Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
	fb->GetCommands()->WaitForCommands(false, false);
	auto data = (const float *)buf->Map(0, total);
	for (int i = 0, w = mHizW, h = mHizH; i < mHizMips; i++, w = std::max(w / 2, 1), h = std::max(h / 2, 1))
	{
		const float *m = data + offs[i] / 4;
		float mx = 0, mn = 2;
		int ones = 0;
		for (int k = 0; k < w * h; k++) { mx = std::max(mx, m[k]); mn = std::min(mn, m[k]); ones += m[k] >= 1.0f; }
		Printf("Spike05 hiz mip %d %dx%d min %g max %g ones %d\n", i, w, h, mn, mx, ones);
		if (w * h <= 64)
		{
			FString line;
			for (int y = 0; y < h; y++)
			{
				for (int x = 0; x < w; x++)
					line.AppendFormat(" %.4f", m[y * w + x]);
				line += " |";
			}
			Printf("   %s\n", line.GetChars());
		}
	}
	buf->Unmap();
}

void VkSpikeCull::Render(const SpikeView &view)
{
	if (!mSupported)
		return;

	BeginFrameStats();
	if (mClusterCount == 0)
		return;

	auto rs = fb->GetRenderState();
	rs->EndRenderPass();

	int vpx, vpy, vpw, vph;
	rs->SpikeGetViewport(vpx, vpy, vpw, vph);
	if (vpw <= 0 || vph <= 0)
		return;
	EnsureHiz(vpw, vph);
	UpdateCullDescriptors();

	int mode = view.mode;
	Slot &slot = mSlots[mCurSlot];
	slot.mode = mode;

	auto cmd = fb->GetCommands()->GetDrawCommands();
	Timestamp(cmd, 0);

	if (mPendingCount > 0)
	{
		cmd->bindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, mScatterPipeline.get());
		cmd->bindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, mScatterLayout.get(), 0, mScatterSets[mStagingIndex].get());
		cmd->pushConstants(mScatterLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &mPendingCount);
		cmd->dispatch((mPendingCount + 63) / 64, 1, 1);
		mPendingCount = 0;
	}
	cmd->fillBuffer(mCounters->buffer, 0, 64, 0);
	PipelineBarrier()
		.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT)
		.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);

	if (mode == SPIKE_HIZ)
	{
		Cull(cmd, view, 1, true);
		Timestamp(cmd, 1);
		DrawIndirect(mCmd1.get(), 0, view);
		cmd = fb->GetCommands()->GetDrawCommands();
		Timestamp(cmd, 2);
		BuildHiz(cmd, vpx, vpy);
		if ((r_spike_debug & 8) && (mFrame % 50) == 0)
		{
			DumpHiz(cmd);
			cmd = fb->GetCommands()->GetDrawCommands();
		}
		Timestamp(cmd, 3);
		Cull(cmd, view, 2, true);
		Timestamp(cmd, 4);
		DrawIndirect(mCmd2.get(), 4, view);
	}
	else if (mode == SPIKE_FRUSTUM)
	{
		Cull(cmd, view, 2, false);
		Timestamp(cmd, 4);
		DrawIndirect(mCmd2.get(), 4, view);
	}
	else
	{
		DrawIndirect(nullptr, 0, view);
	}
	cmd = fb->GetCommands()->GetDrawCommands();
	Timestamp(cmd, 5);

	if (mode != SPIKE_NOCULL)
	{
		PipelineBarrier()
			.AddMemory(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT)
			.Execute(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		VkBufferCopy region = { 0, (VkDeviceSize)mCurSlot * 64, 64 };
		cmd->copyBuffer(mCounters->buffer, mReadback->buffer, 1, &region);
		PipelineBarrier()
			.AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT)
			.Execute(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
		slot.counters = true;
	}
}
