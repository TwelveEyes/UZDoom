/*
** ClusterGrid.h
**
** UZDoom-only (plan M6.1): the froxel grid of the clustered forward+ dynamic lights. 64x64 pixel tiles x 24
** logarithmic depth slices between the near plane and the far distance (frustum far or MaxDistance). The grid is
** pure math (no engine state) so the CPU assignment, the compute shader and the fragment lookup can be checked
** against each other.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Conventions (all of them are shared with the GLSL in ClusterAssignProgram.cpp / ClusterFragPrelude.cpp):
**
** - View space is the eye space of HWViewpointUniforms::ViewMatrix applied to the frame of lights[] / pixelpos
**   (x, height, y); the camera looks down -z, depth = -z.
** - A cluster is addressed x + y * CountX + slice * CountX * CountY.
** - Pixel coordinates are gl_FragCoord.xy. The viewport rectangle (x, y, width, height) is in that space, for both
**   GL and Vulkan (the scene projection is not flipped on either backend, so fragcoord.y = 0 is NDC y = -1).
** - Slice k covers depth [near * (far/near)^(k/24), near * (far/near)^((k+1)/24)]. A fragment's slice is
**   floor(log(depth) * SliceScale + SliceBias), clamped to the grid.
** - The cluster boxes are padded by a small relative amount so that float rounding differences between the
**   fragment's log() lookup, the CPU and the compute shader can never make a light miss a cluster it touches.
**
*/

#pragma once

#include <cstdint>
#include <vector>

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

// Fragment-side block (std140). Names are the GLSL members of ClusterParams (ClusterFragPrelude).
struct ClusterParamsUniform
{
	float Range[4];			// near, far, SliceScale, SliceBias
	float Viewport[4];		// x, y, width, height in gl_FragCoord space
	int32_t Grid[4];		// CountX, CountY, slices, tile pixels
	int32_t State[4];		// x = 1 when the offset buffer is valid for this view
};
static_assert(sizeof(ClusterParamsUniform) == 64, "ClusterParamsUniform is a std140 block");

class ClusterGrid
{
public:
	static constexpr int TilePixels = 64;
	static constexpr int Slices = 24;

	// projection / view are the column-major 16 floats of VSMatrix::get().
	// Returns true when the grid changed (and the cluster boxes were recomputed).
	bool Setup(int viewportX, int viewportY, int width, int height, const float* projection, float nearZ, float farZ);

	bool IsValid() const { return m_count > 0; }
	int CountX() const { return m_countX; }
	int CountY() const { return m_countY; }
	int Count() const { return m_count; }
	float Near() const { return m_near; }
	float Far() const { return m_far; }
	int ViewportX() const { return m_viewportX; }
	int ViewportY() const { return m_viewportY; }
	int Width() const { return m_width; }
	int Height() const { return m_height; }
	float SliceScale() const { return m_sliceScale; }
	float SliceBias() const { return m_sliceBias; }
	// sx, sy, ox, oy of the projection: x_ndc = sx * x / depth - ox (and y likewise).
	const float* Projection() const { return m_proj; }

	// Depth of the near edge of a slice (slice == Slices gives far).
	float SliceDepth(int slice) const;

	// The slice of a view depth (the same formula the fragment shader evaluates), clamped to [0, Slices - 1].
	int SliceOf(float depth) const;

	// The padded view space box of a cluster: {minx, miny, minz, maxx, maxy, maxz}.
	const float* Bounds(int cluster) const { return &m_bounds[(size_t)cluster * 6]; }

	ClusterParamsUniform FragmentParams(bool valid) const;

private:
	void ComputeBounds();

	int m_viewportX = 0, m_viewportY = 0, m_width = 0, m_height = 0;
	int m_countX = 0, m_countY = 0, m_count = 0;
	float m_near = 0, m_far = 0;
	float m_sliceScale = 0, m_sliceBias = 0;
	float m_proj[4] = {};
	std::vector<float> m_bounds;
};

}
