/*
** ClusterGrid.cpp
**
** UZDoom-only (plan M6.1): see ClusterGrid.h.
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
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterGrid.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

bool ClusterGrid::Setup(int viewportX, int viewportY, int width, int height, const float* projection, float nearZ, float farZ)
{
	width = std::max(width, 1);
	height = std::max(height, 1);
	nearZ = std::max(nearZ, 0.001f);
	farZ = std::max(farZ, nearZ * 2.0f);

	float proj[4] = { projection[0], projection[5], projection[8], projection[9] };

	bool unchanged = m_count > 0 && viewportX == m_viewportX && viewportY == m_viewportY && width == m_width && height == m_height
		&& nearZ == m_near && farZ == m_far && std::equal(proj, proj + 4, m_proj);
	if (unchanged)
		return false;

	m_viewportX = viewportX;
	m_viewportY = viewportY;
	m_width = width;
	m_height = height;
	m_near = nearZ;
	m_far = farZ;
	std::copy(proj, proj + 4, m_proj);

	m_countX = (width + TilePixels - 1) / TilePixels;
	m_countY = (height + TilePixels - 1) / TilePixels;
	m_count = m_countX * m_countY * Slices;

	float logRatio = std::log(m_far / m_near);
	m_sliceScale = (float)Slices / logRatio;
	m_sliceBias = -std::log(m_near) * m_sliceScale;

	ComputeBounds();
	return true;
}

float ClusterGrid::SliceDepth(int slice) const
{
	if (slice <= 0)
		return m_near;
	if (slice >= Slices)
		return m_far;
	return m_near * std::pow(m_far / m_near, (float)slice / (float)Slices);
}

int ClusterGrid::SliceOf(float depth) const
{
	float s = std::floor(std::log(std::max(depth, m_near)) * m_sliceScale + m_sliceBias);
	if (!(s > 0.0f))
		return 0;
	return std::min((int)s, Slices - 1);
}

void ClusterGrid::ComputeBounds()
{
	m_bounds.resize((size_t)m_count * 6);

	const float sx = m_proj[0], sy = m_proj[1], ox = m_proj[2], oy = m_proj[3];
	const float fw = (float)m_width, fh = (float)m_height;

	std::vector<float> depth(Slices + 1);
	for (int k = 0; k <= Slices; k++)
		depth[k] = SliceDepth(k);

	for (int slice = 0; slice < Slices; slice++)
	{
		float dn = depth[slice], df = depth[slice + 1];
		float pad = 1.0e-3f * df + 1.0e-2f;

		for (int ty = 0; ty < m_countY; ty++)
		{
			float py0 = (float)(ty * TilePixels);
			float py1 = std::min((float)((ty + 1) * TilePixels), fh);
			float ny0 = 2.0f * py0 / fh - 1.0f;
			float ny1 = 2.0f * py1 / fh - 1.0f;

			for (int tx = 0; tx < m_countX; tx++)
			{
				float px0 = (float)(tx * TilePixels);
				float px1 = std::min((float)((tx + 1) * TilePixels), fw);
				float nx0 = 2.0f * px0 / fw - 1.0f;
				float nx1 = 2.0f * px1 / fw - 1.0f;

				float mn[3] = { 1.0e30f, 1.0e30f, 1.0e30f };
				float mx[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
				for (int c = 0; c < 8; c++)
				{
					float xn = (c & 1) ? nx1 : nx0;
					float yn = (c & 2) ? ny1 : ny0;
					float d = (c & 4) ? df : dn;
					float p[3] = { d * (xn + ox) / sx, d * (yn + oy) / sy, -d };
					for (int a = 0; a < 3; a++)
					{
						mn[a] = std::min(mn[a], p[a]);
						mx[a] = std::max(mx[a], p[a]);
					}
				}

				float* b = &m_bounds[(size_t)(tx + ty * m_countX + slice * m_countX * m_countY) * 6];
				for (int a = 0; a < 3; a++)
				{
					b[a] = mn[a] - pad;
					b[3 + a] = mx[a] + pad;
				}
			}
		}
	}
}

ClusterParamsUniform ClusterGrid::FragmentParams(bool valid) const
{
	ClusterParamsUniform p = {};
	p.Range[0] = m_near;
	p.Range[1] = m_far;
	p.Range[2] = m_sliceScale;
	p.Range[3] = m_sliceBias;
	p.Viewport[0] = (float)m_viewportX;
	p.Viewport[1] = (float)m_viewportY;
	p.Viewport[2] = (float)m_width;
	p.Viewport[3] = (float)m_height;
	p.Grid[0] = m_countX;
	p.Grid[1] = m_countY;
	p.Grid[2] = Slices;
	p.Grid[3] = TilePixels;
	p.State[0] = valid ? 1 : 0;
	return p;
}

}
