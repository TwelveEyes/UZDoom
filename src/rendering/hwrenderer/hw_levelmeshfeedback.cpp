/*
** hw_levelmeshgather.cpp
**
** Levelmesh actor gather: CPU frustum test of a cluster box
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The actor gather (HWDrawInfo::GatherLevelMeshThings, ticket 17) takes the
** clusters the previous frame's GPU cull found visible and intersects them
** with the current frustum. The test is the rejection half of cull.comp's
** ProjectBox: the box's 8 corners in clip space, rejected only when all of
** them are outside one of the x / y planes or behind the eye (no near / far
** rejection: the scene draws with depth clamp).
*/

// HEADER FILES ------------------------------------------------------------

#include "hw_levelmeshbake.h"

// CODE --------------------------------------------------------------------

bool LevelMesh_ClusterInFrustum(const float *viewProj, const LevelMeshCluster &c)
{
	const float *m = viewProj;	// column-major, GL world space (x, z, y)
	unsigned allOut = 31;
	for (int i = 0; i < 8; i++)
	{
		float x = (i & 1) ? c.bmax[0] : c.bmin[0];
		float y = (i & 2) ? c.bmax[1] : c.bmin[1];
		float z = (i & 4) ? c.bmax[2] : c.bmin[2];
		// Map space -> GL world space: (x, z, y).
		float cx = m[0] * x + m[4] * z + m[8] * y + m[12];
		float cy = m[1] * x + m[5] * z + m[9] * y + m[13];
		float cw = m[3] * x + m[7] * z + m[11] * y + m[15];
		unsigned codes = 0;
		if (cx < -cw) codes |= 1;
		if (cx > cw) codes |= 2;
		if (cy < -cw) codes |= 4;
		if (cy > cw) codes |= 8;
		if (cw <= 0.f) codes |= 16;
		allOut &= codes;
		if (allOut == 0) return true;
	}
	return false;
}
