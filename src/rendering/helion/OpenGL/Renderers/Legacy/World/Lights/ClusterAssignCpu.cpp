/*
** ClusterAssignCpu.cpp
**
** UZDoom-only (plan M6.1): see ClusterAssignCpu.h. Keep the arithmetic of Hits / Transform in step with the
** functions of the same name in the compute shader (ClusterAssignProgram.cpp).
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
#include <cmath>
#include <cstring>
#include <map>
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterAssignCpu.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

ClusterAssignCpu::ViewLight ClusterAssignCpu::Transform(const float* m, const ClusterLight& light)
{
	ViewLight v;
	const float* f = light.f;
	v.c[0] = m[0] * f[0] + m[4] * f[1] + m[8] * f[2] + m[12];
	v.c[1] = m[1] * f[0] + m[5] * f[1] + m[9] * f[2] + m[13];
	v.c[2] = m[2] * f[0] + m[6] * f[1] + m[10] * f[2] + m[14];
	v.r = f[3];
	v.spot = f[11] > 0.5f;
	v.cosOuter = f[13];
	v.sinOuter = std::sqrt(std::max(0.0f, 1.0f - f[13] * f[13]));

	// The stored direction points from the lit surface back to the light, the cone axis is the opposite.
	float d[3] = {
		-(m[0] * f[8] + m[4] * f[9] + m[8] * f[10]),
		-(m[1] * f[8] + m[5] * f[9] + m[9] * f[10]),
		-(m[2] * f[8] + m[6] * f[9] + m[10] * f[10]),
	};
	float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	float inv = len > 1.0e-12f ? 1.0f / len : 0.0f;
	v.axis[0] = d[0] * inv;
	v.axis[1] = d[1] * inv;
	v.axis[2] = d[2] * inv;
	return v;
}

bool ClusterAssignCpu::Hits(const ViewLight& light, const float* box, float& dsq)
{
	float dx = light.c[0] - std::min(std::max(light.c[0], box[0]), box[3]);
	float dy = light.c[1] - std::min(std::max(light.c[1], box[1]), box[4]);
	float dz = light.c[2] - std::min(std::max(light.c[2], box[2]), box[5]);
	dsq = dx * dx + dy * dy + dz * dz;
	if (dsq > light.r * light.r)
		return false;

	if (light.spot && light.cosOuter > 0.0f)
	{
		// cone against the sphere around the box
		float bc[3] = { (box[0] + box[3]) * 0.5f, (box[1] + box[4]) * 0.5f, (box[2] + box[5]) * 0.5f };
		float ex = box[3] - box[0], ey = box[4] - box[1], ez = box[5] - box[2];
		float br = std::sqrt(ex * ex + ey * ey + ez * ez) * 0.5f;

		float v[3] = { bc[0] - light.c[0], bc[1] - light.c[1], bc[2] - light.c[2] };
		float vlen2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
		float v1 = v[0] * light.axis[0] + v[1] * light.axis[1] + v[2] * light.axis[2];
		float closest = light.cosOuter * std::sqrt(std::max(vlen2 - v1 * v1, 0.0f)) - v1 * light.sinOuter;
		if (closest > br || v1 > br + light.r || v1 < -br)
			return false;
	}
	return true;
}

namespace
{

struct Pair
{
	uint32_t Cluster;
	uint32_t Light;
	float Dsq;
};

// The clusters a light can touch: slices from the depth interval, then per slice the tile columns / rows whose padded box
// overlaps the light's bounding cube on x / y. The box extent on x depends on the tile column and slice only (on y: row and
// slice), so the exact test per axis is cheap and the result is a superset of what Hits accepts.
struct ClusterRange
{
	int s0, s1;
	bool Empty;
};

ClusterRange CandidateSlices(const ClusterGrid& grid, const ClusterAssignCpu::ViewLight& l)
{
	ClusterRange range = {};
	range.Empty = true;

	float depth = -l.c[2];
	float rad = l.r * 1.002f + 0.05f;
	float d0 = std::max((depth - rad), grid.Near());
	float d1 = std::min((depth + rad), grid.Far());
	if (d1 < d0)
		return range;

	range.s0 = std::max(grid.SliceOf(d0) - 1, 0);
	range.s1 = std::min(grid.SliceOf(d1) + 1, ClusterGrid::Slices - 1);
	range.Empty = range.s0 > range.s1;
	return range;
}

}

void ClusterAssignCpu::Assign(const ClusterGrid& grid, const float* view, const ClusterLight* lights, int lightCount,
	const ClusterAssignOptions& options, ClusterAssignResult& result)
{
	const int clusterCount = grid.Count();
	result.Offsets.assign((size_t)clusterCount, -1);
	result.Lights.clear();
	result.UsedVec4 = 0;
	result.OverflowClusters = 0;
	result.DroppedClusters = 0;
	if (clusterCount <= 0 || lightCount <= 0)
		return;

	// 1. every (cluster, light) pair that passes the exact test, in ascending light order
	std::vector<Pair> pairs;
	std::vector<uint32_t> counts((size_t)clusterCount + 1, 0);
	const int cx = grid.CountX(), cy = grid.CountY();
	for (int li = 0; li < lightCount; li++)
	{
		const ClusterLight& light = lights[li];
		if (!(light.f[3] > 0.0f))
			continue;

		ViewLight vl = Transform(view, light);
		ClusterRange range = CandidateSlices(grid, vl);
		if (range.Empty)
			continue;

		const float rad = vl.r * 1.002f + 0.05f;
		for (int s = range.s0; s <= range.s1; s++)
		{
			int x0 = cx, x1 = -1, y0 = cy, y1 = -1;
			for (int x = 0; x < cx; x++)
			{
				const float* b = grid.Bounds(x + s * cx * cy);
				if (b[3] >= vl.c[0] - rad && b[0] <= vl.c[0] + rad)
				{
					x0 = std::min(x0, x);
					x1 = std::max(x1, x);
				}
			}
			for (int y = 0; y < cy; y++)
			{
				const float* b = grid.Bounds(y * cx + s * cx * cy);
				if (b[4] >= vl.c[1] - rad && b[1] <= vl.c[1] + rad)
				{
					y0 = std::min(y0, y);
					y1 = std::max(y1, y);
				}
			}

			for (int y = y0; y <= y1; y++)
			{
				for (int x = x0; x <= x1; x++)
				{
					int cluster = x + y * cx + s * cx * cy;
					float dsq;
					if (Hits(vl, grid.Bounds(cluster), dsq))
					{
						pairs.push_back({ (uint32_t)cluster, (uint32_t)li, dsq });
						counts[cluster + 1]++;
					}
				}
			}
		}
	}
	if (pairs.empty())
		return;

	// 2. stable counting sort by cluster
	for (int c = 0; c < clusterCount; c++)
		counts[c + 1] += counts[c];
	std::vector<Pair> sorted(pairs.size());
	{
		std::vector<uint32_t> fill(counts.begin(), counts.end() - 1);
		for (const Pair& p : pairs)
			sorted[fill[p.Cluster]++] = p;
	}

	// 3. per cluster: overflow selection, bucket order, size
	struct Cluster
	{
		std::vector<uint32_t> Kept;		// bucket ordered light indices
		uint32_t N[3] = { 0, 0, 0 };
		uint32_t Size = 0;
	};
	std::vector<Cluster> clusters;
	std::vector<uint32_t> clusterId(clusterCount, 0xffffffffu);
	std::vector<Pair> work;
	for (int c = 0; c < clusterCount; c++)
	{
		uint32_t begin = counts[c], end = counts[c + 1];
		if (begin == end)
			continue;

		work.assign(sorted.begin() + begin, sorted.begin() + end);
		if ((int)work.size() > options.MaxPerCluster)
		{
			result.OverflowClusters++;
			std::stable_sort(work.begin(), work.end(), [](const Pair& a, const Pair& b) {
				return a.Dsq < b.Dsq || (a.Dsq == b.Dsq && a.Light < b.Light);
			});
			work.resize((size_t)options.MaxPerCluster);
			std::sort(work.begin(), work.end(), [](const Pair& a, const Pair& b) { return a.Light < b.Light; });
		}

		Cluster cl;
		for (int bucket = 0; bucket < 3; bucket++)
		{
			for (const Pair& p : work)
			{
				if ((int)lights[p.Light].f[14] == bucket)
				{
					cl.Kept.push_back(p.Light);
					cl.N[bucket]++;
				}
			}
		}
		cl.Size = 1 + 4 * (uint32_t)cl.Kept.size();
		clusterId[c] = (uint32_t)clusters.size();
		clusters.push_back(std::move(cl));
	}

	// 4. placement
	std::map<std::vector<uint32_t>, uint32_t> dedupe;
	uint32_t run = 0;
	std::vector<uint32_t> placed;	// cluster ids in placement order of their (unique) records
	std::vector<uint32_t> placedAt;
	for (int c = 0; c < clusterCount; c++)
	{
		if (clusterId[c] == 0xffffffffu)
			continue;
		const Cluster& cl = clusters[clusterId[c]];

		if (options.Dedupe)
		{
			auto it = dedupe.find(cl.Kept);
			if (it != dedupe.end())
			{
				result.Offsets[c] = (int32_t)it->second;
				continue;
			}
			if (run + cl.Size > options.CapacityVec4)
			{
				result.DroppedClusters++;
				continue;
			}
			dedupe.emplace(cl.Kept, run);
			result.Offsets[c] = (int32_t)run;
			placed.push_back(clusterId[c]);
			placedAt.push_back(run);
			run += cl.Size;
		}
		else
		{
			// plain prefix sum over all clusters; a cluster that does not fit is dropped but still advances the sum
			if (run + cl.Size <= options.CapacityVec4)
			{
				result.Offsets[c] = (int32_t)run;
				placed.push_back(clusterId[c]);
				placedAt.push_back(run);
			}
			else
			{
				result.DroppedClusters++;
			}
			run += cl.Size;
		}
	}

	// 5. records
	uint32_t used = 0;
	for (size_t i = 0; i < placed.size(); i++)
		used = std::max(used, placedAt[i] + clusters[placed[i]].Size);
	result.UsedVec4 = used;
	result.Lights.assign((size_t)used * 4, 0.0f);
	for (size_t i = 0; i < placed.size(); i++)
	{
		const Cluster& cl = clusters[placed[i]];
		float* out = &result.Lights[(size_t)placedAt[i] * 4];
		out[0] = 0.0f;
		out[1] = (float)(4 * cl.N[0]);
		out[2] = (float)(4 * (cl.N[0] + cl.N[1]));
		out[3] = (float)(4 * (cl.N[0] + cl.N[1] + cl.N[2]));
		out += 4;
		for (uint32_t li : cl.Kept)
		{
			std::memcpy(out, lights[li].f, sizeof(float) * 16);
			out[14] = 0.0f;
			out[15] = 0.0f;
			out += 16;
		}
	}
}

}
