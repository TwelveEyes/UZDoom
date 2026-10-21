/*
** ClusterAssignProgram.cpp
**
** UZDoom-only (plan M6.1): see ClusterAssignProgram.h. The GLSL below and ClusterAssignCpu.cpp / ClusterGrid.cpp
** implement the same arithmetic: change them together.
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

#include <algorithm>
#include <cstring>
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterAssignProgram.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterFragPrelude.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "v_video.h"
#include "engineerrors.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

namespace
{

const int WorkgroupSize = 64;

// The members are globals of the shader; the names are shared with ClusterFragPrelude (uClRange, uClViewport, uClGrid).
const char* ParamsBlockBody = R"GLSL({
	mat4 uClView;
	vec4 uClProj;
	vec4 uClRange;
	vec4 uClViewport;
	ivec4 uClGrid;
	ivec4 uClLimits;
})GLSL";

const char* LightsInBody = "{\n\tvec4 uClLights[];\n}";
const char* CountsBody = "{\n\tuint uClCounts[];\n}";
const char* OffsetsBody = "{\n\tint clusterOffsets[];\n}";
const char* LightsOutBody = "{\n\tvec4 lights[];\n}";

}

FString ClusterAssignProgram::ComputeShader()
{
	// uClLimits: x = max lights per cluster, y = light count, z = capacity in vec4, w = pass
	return R"GLSL(
shared uint scanSums[64];

// Box of a cluster in view space. Keep in step with ClusterGrid::ComputeBounds.
void clusterBounds(int cluster, out vec3 bmin, out vec3 bmax)
{
	int cx = uClGrid.x;
	int cy = uClGrid.y;
	int ns = uClGrid.z;
	int tile = uClGrid.w;
	int tx = cluster % cx;
	int ty = (cluster / cx) % cy;
	int tz = cluster / (cx * cy);

	float fw = uClViewport.z;
	float fh = uClViewport.w;
	float px0 = float(tx * tile);
	float px1 = min(float((tx + 1) * tile), fw);
	float py0 = float(ty * tile);
	float py1 = min(float((ty + 1) * tile), fh);
	float nx0 = 2.0 * px0 / fw - 1.0;
	float nx1 = 2.0 * px1 / fw - 1.0;
	float ny0 = 2.0 * py0 / fh - 1.0;
	float ny1 = 2.0 * py1 / fh - 1.0;

	float ratio = uClRange.y / uClRange.x;
	float dn = (tz <= 0) ? uClRange.x : uClRange.x * pow(ratio, float(tz) / float(ns));
	float df = (tz + 1 >= ns) ? uClRange.y : uClRange.x * pow(ratio, float(tz + 1) / float(ns));
	float pad = 1.0e-3 * df + 1.0e-2;

	bmin = vec3(1.0e30);
	bmax = vec3(-1.0e30);
	for (int c = 0; c < 8; c++)
	{
		float xn = ((c & 1) != 0) ? nx1 : nx0;
		float yn = ((c & 2) != 0) ? ny1 : ny0;
		float d = ((c & 4) != 0) ? df : dn;
		vec3 p = vec3(d * (xn + uClProj.z) / uClProj.x, d * (yn + uClProj.w) / uClProj.y, -d);
		bmin = min(bmin, p);
		bmax = max(bmax, p);
	}
	bmin -= vec3(pad);
	bmax += vec3(pad);
}

// Sphere (and for spot lights cone) against the cluster box. Keep in step with ClusterAssignCpu::Hits.
bool clusterHit(int li, vec3 bmin, vec3 bmax, out float dsq)
{
	vec4 l0 = uClLights[li * 4];
	dsq = 0.0;
	float r = l0.w;
	if (!(r > 0.0))
		return false;

	vec3 c = (uClView * vec4(l0.xyz, 1.0)).xyz;
	vec3 dv = c - clamp(c, bmin, bmax);
	dsq = dot(dv, dv);
	if (dsq > r * r)
		return false;

	vec4 l2 = uClLights[li * 4 + 2];
	vec4 l3 = uClLights[li * 4 + 3];
	if (l2.w > 0.5 && l3.y > 0.0)
	{
		float cosOuter = l3.y;
		float sinOuter = sqrt(max(0.0, 1.0 - cosOuter * cosOuter));
		vec3 axis = -(mat3(uClView) * l2.xyz);
		float len = length(axis);
		axis = (len > 1.0e-12) ? axis / len : vec3(0.0);

		vec3 bc = (bmin + bmax) * 0.5;
		float br = length(bmax - bmin) * 0.5;
		vec3 v = bc - c;
		float vlen2 = dot(v, v);
		float v1 = dot(v, axis);
		float closest = cosOuter * sqrt(max(vlen2 - v1 * v1, 0.0)) - v1 * sinOuter;
		if (closest > br || v1 > br + r || v1 < -br)
			return false;
	}
	return true;
}

int clusterTotal(vec3 bmin, vec3 bmax)
{
	int total = 0;
	int n = uClLimits.y;
	for (int li = 0; li < n; li++)
	{
		float d;
		if (clusterHit(li, bmin, bmax, d))
			total++;
	}
	return total;
}

// li hits the cluster. When more than the limit hit it, the nearest ones (squared distance, then index) are kept.
bool clusterKeep(int li, vec3 bmin, vec3 bmax, int total)
{
	int limit = uClLimits.x;
	if (total <= limit)
		return true;

	float dl;
	clusterHit(li, bmin, bmax, dl);
	int rank = 0;
	int n = uClLimits.y;
	for (int k = 0; k < n; k++)
	{
		if (k == li)
			continue;
		float dk;
		if (clusterHit(k, bmin, bmax, dk) && (dk < dl || (dk == dl && k < li)))
			rank++;
	}
	return rank < limit;
}

void main()
{
	int pass = uClLimits.w;
	int clusterCount = uClGrid.x * uClGrid.y * uClGrid.z;

	if (pass == 0)
	{
		int c = int(gl_GlobalInvocationID.x);
		if (c >= clusterCount)
			return;

		vec3 bmin, bmax;
		clusterBounds(c, bmin, bmax);
		int total = clusterTotal(bmin, bmax);
		int kept = 0;
		if (total > 0)
		{
			int n = uClLimits.y;
			for (int li = 0; li < n; li++)
			{
				float d;
				if (clusterHit(li, bmin, bmax, d) && clusterKeep(li, bmin, bmax, total))
					kept++;
			}
		}
		uClCounts[c] = (kept > 0) ? uint(1 + 4 * kept) : 0u;
	}
	else if (pass == 1)
	{
		uint t = gl_LocalInvocationID.x;
		uint count = uint(clusterCount);
		uint chunk = (count + 63u) / 64u;
		uint begin = min(t * chunk, count);
		uint end = min(begin + chunk, count);

		uint sum = 0u;
		for (uint i = begin; i < end; i++)
			sum += uClCounts[i];
		scanSums[t] = sum;
		memoryBarrierShared();
		barrier();

		if (t == 0u)
		{
			uint run = 0u;
			for (int i = 0; i < 64; i++)
			{
				uint v = scanSums[i];
				scanSums[i] = run;
				run += v;
			}
		}
		memoryBarrierShared();
		barrier();

		uint run = scanSums[t];
		uint capacity = uint(uClLimits.z);
		for (uint i = begin; i < end; i++)
		{
			uint size = uClCounts[i];
			if (size == 0u)
			{
				clusterOffsets[i] = -1;
			}
			else
			{
				clusterOffsets[i] = (run + size <= capacity) ? int(run) : -1;
				run += size;
			}
		}
	}
	else
	{
		int c = int(gl_GlobalInvocationID.x);
		if (c >= clusterCount)
			return;
		int offset = clusterOffsets[c];
		if (offset < 0)
			return;

		vec3 bmin, bmax;
		clusterBounds(c, bmin, bmax);
		int total = clusterTotal(bmin, bmax);
		int n = uClLimits.y;

		int counts[3] = int[3](0, 0, 0);
		for (int li = 0; li < n; li++)
		{
			float d;
			if (clusterHit(li, bmin, bmax, d) && clusterKeep(li, bmin, bmax, total))
				counts[clamp(int(uClLights[li * 4 + 3].z), 0, 2)]++;
		}
		lights[offset] = vec4(0.0, float(4 * counts[0]), float(4 * (counts[0] + counts[1])), float(4 * (counts[0] + counts[1] + counts[2])));

		int pos = offset + 1;
		for (int bucket = 0; bucket < 3; bucket++)
		{
			for (int li = 0; li < n; li++)
			{
				float d;
				if (clamp(int(uClLights[li * 4 + 3].z), 0, 2) == bucket && clusterHit(li, bmin, bmax, d) && clusterKeep(li, bmin, bmax, total))
				{
					lights[pos] = uClLights[li * 4];
					lights[pos + 1] = uClLights[li * 4 + 1];
					lights[pos + 2] = uClLights[li * 4 + 2];
					vec4 l3 = uClLights[li * 4 + 3];
					l3.z = 0.0;
					l3.w = 0.0;
					lights[pos + 3] = l3;
					pos += 4;
				}
			}
		}
	}
}
)GLSL";
}

RenderProgramDesc ClusterAssignProgram::MakeDesc()
{
	RenderProgramDesc desc;
	desc.name = "ClusterAssign";
	desc.computeSource = ComputeShader();
	desc.isCompute = true;
	desc.computeLocalSize[0] = WorkgroupSize;
	desc.computeLocalSize[1] = 1;
	desc.computeLocalSize[2] = 1;

	auto add = [&](const char* name, RenderProgramBinding::Kind kind, int binding, const char* type, const char* body, const char* qualifiers)
	{
		RenderProgramBinding b;
		b.name = name;
		b.kind = kind;
		b.binding = binding;
		b.vertex = b.fragment = false;
		b.compute = true;
		b.blockTypeName = type;
		b.blockBody = body;
		b.qualifiers = qualifiers;
		desc.bindings.Push(b);
	};
	add("", RenderProgramBinding::UniformBlock, BindParams, "ClusterAssignParams", ParamsBlockBody, "");
	add("", RenderProgramBinding::StorageBuffer, BindLightsIn, "ClusterLightInputBuffer", LightsInBody, "readonly");
	add("", RenderProgramBinding::StorageBuffer, BindCounts, "ClusterCountBuffer", CountsBody, "");
	add("", RenderProgramBinding::StorageBuffer, BindOffsets, "ClusterOffsetBuffer", OffsetsBody, "");
	add("", RenderProgramBinding::StorageBuffer, BindLightsOut, "LightBufferSSO", LightsOutBody, "writeonly");
	return desc;
}

void ClusterAssignProgram::CreateShared()
{
	s_shared.reset();
	if (!screen->SupportsCompute() || !ClusterFragPrelude::UsesStorageBuffers())
		return;

	std::unique_ptr<ClusterAssignProgram> program(new ClusterAssignProgram());
	// Compile errors are fatal (I_FatalError("Render program %s: ...")) in the backend, like for every render program.
	program->m_program.reset(screen->CreateRenderProgram(MakeDesc()));
	if (program->m_program == nullptr || !program->m_program->IsValid())
		I_FatalError("Render program ClusterAssign: error creating the program");
	s_shared = std::move(program);
}

ClusterAssignProgram::~ClusterAssignProgram() = default;

void ClusterAssignProgram::Dispatch(const ClusterGrid& grid, const float* view, int lightCount, int maxPerCluster, uint32_t capacityVec4,
	IBuffer* lightsIn, IBuffer* counts, IBuffer* offsets, IBuffer* lightsOut)
{
	ClusterAssignUniforms u = {};
	std::memcpy(u.View, view, sizeof(u.View));
	std::memcpy(u.Proj, grid.Projection(), sizeof(u.Proj));
	u.Range[0] = grid.Near();
	u.Range[1] = grid.Far();
	u.Range[2] = grid.SliceScale();
	u.Range[3] = grid.SliceBias();
	u.Viewport[0] = (float)grid.ViewportX();
	u.Viewport[1] = (float)grid.ViewportY();
	u.Viewport[2] = (float)grid.Width();
	u.Viewport[3] = (float)grid.Height();
	u.Grid[0] = grid.CountX();
	u.Grid[1] = grid.CountY();
	u.Grid[2] = ClusterGrid::Slices;
	u.Grid[3] = ClusterGrid::TilePixels;
	u.Limits[0] = maxPerCluster;
	u.Limits[1] = lightCount;
	u.Limits[2] = (int32_t)capacityVec4;

	auto& state = *screen->RenderState();
	state.SetProgram(m_program.get());
	state.SetProgramBuffer(BindLightsIn, lightsIn, 0, 0);
	state.SetProgramBuffer(BindCounts, counts, 0, 0);
	state.SetProgramBuffer(BindOffsets, offsets, 0, 0);
	state.SetProgramBuffer(BindLightsOut, lightsOut, 0, 0);

	const int groups = (grid.Count() + WorkgroupSize - 1) / WorkgroupSize;

	// Everything that read the buffers before (the previous view's draws) is finished before the count pass writes them.
	screen->MemoryBarrierCompute();

	u.Limits[3] = 0;
	state.SetProgramUniforms(BindParams, &u, sizeof(u));
	screen->DispatchCompute(m_program.get(), groups, 1, 1);
	screen->MemoryBarrierCompute();

	u.Limits[3] = 1;
	state.SetProgramUniforms(BindParams, &u, sizeof(u));
	screen->DispatchCompute(m_program.get(), 1, 1, 1);
	screen->MemoryBarrierCompute();

	u.Limits[3] = 2;
	state.SetProgramUniforms(BindParams, &u, sizeof(u));
	screen->DispatchCompute(m_program.get(), groups, 1, 1);
	screen->MemoryBarrierCompute();

	state.SetProgram(nullptr);
}

}
