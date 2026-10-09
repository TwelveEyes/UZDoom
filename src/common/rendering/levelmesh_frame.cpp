/*
** levelmesh_frame.cpp
**
** Per-frame frame build for the backend-neutral level-mesh data layer:
** view-frustum plane derivation and the per-(region, texture) sub-range
** cull walk that produces the compact draw list. See levelmesh_frame.h.
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

// HEADER FILES ------------------------------------------------------------

#include <math.h>

#include "levelmesh_frame.h"
#include "levelmesh.h"
#include "g_levellocals.h"
#include "d_player.h"
#include "i_time.h"	// I_GetTimeFrac (FOV interpolation fraction)
#include "r_utility.h"	// r_viewwindow (widescreen ratio of the main view)
#include "matrix.h"

// CODE --------------------------------------------------------------------

namespace levelmesh
{
//============================================================================
//
// Cull frustum constants.
//
// LM_CULL_ZNEAR / LM_CULL_ZFAR mirror DFrameBuffer::GetZNear() / GetZFar():
// the cull frustum must match the classic projection, so keep them in sync
// if those change.
//
// LM_CULL_ZMAX is the generous fixed upper bound used to extrude the
// 2D sub-range AABBs into 3D for the plane test (see LMAABBInFrustum).
// Level geometry in practice stays far below it; anything higher would be
// outside the classic renderer's useful visibility range anyway.
//
//============================================================================

static constexpr float LM_CULL_ZNEAR = 5.f;
static constexpr float LM_CULL_ZFAR = 65536.f;
static constexpr float LM_CULL_ZMAX = 16384.f;

static float LM_DEG2RAD(float deg)
{
	return deg * (float)(M_PI / 180.0);
}

static float LM_RAD2DEG(float rad)
{
	return rad * (float)(180. / M_PI);
}

//============================================================================
//
// LMAABBInFrustum
//
// The exact cull test: a sub-range entry's 2D world AABB is extruded to two
// z-slices (z=0 and z=LM_CULL_ZMAX), giving eight corner points. The entry
// is kept iff ANY of the eight corners is inside ALL six frustum planes;
// it is culled only if EVERY corner fails at least one plane. For a convex
// box this is exact: all-corners-outside implies the whole box is outside,
// so the test can only ever cull - never keep a box that pokes in. The
// z=0 / z=zMax extrusion is conservative (real walls span between sector
// heights), which keeps visible geometry; it only weakens culling for
// boxes whose real height range is much smaller.
//
//============================================================================

static bool LMAABBInFrustum(const float aabbMin[2], const float aabbMax[2], const float planes[24])
{
	const float xs[2] = { aabbMin[0], aabbMax[0] };
	const float ys[2] = { aabbMin[1], aabbMax[1] };
	const float zs[2] = { 0.f, LM_CULL_ZMAX };

	for (int ix = 0; ix < 2; ix++)
	{
		for (int iy = 0; iy < 2; iy++)
		{
			for (int iz = 0; iz < 2; iz++)
			{
				const float x = xs[ix];
				const float y = ys[iy];
				const float z = zs[iz];
				bool inside = true;
				for (int p = 0; p < 6 && inside; p++)
				{
					const float *pl = &planes[p * 4];
					if (pl[0] * x + pl[1] * y + pl[2] * z + pl[3] < 0.f) inside = false;
				}
				if (inside) return true;
			}
		}
	}
	return false;
}

//============================================================================
//
// Prepare / Build
//
//============================================================================

void FLevelMeshFrame::Prepare(const FLevelMesh &mesh)
{
	uint32_t newBound = 0;
	for (const auto &reg : mesh.regions)
	{
		newBound += reg.texRangeCount;
	}
	if (newBound == bound) return;	// idempotent for an unchanged mesh

	bound = newBound;
	drawList.Clear();
	drawList.Reserve(newBound);	// Count = newBound; no further allocation in Build
}

uint32_t FLevelMeshFrame::Build(const FLevelMesh &mesh, const float planes[24])
{
	Prepare(mesh);	// lazy: allocates at most once per level (re)load
	if (bound == 0) return 0;

	uint32_t count = 0;
	const uint32_t nregions = mesh.regions.Size();
	for (uint32_t r = 0; r < nregions; r++)
	{
		const LevelMeshRegion &reg = mesh.regions[r];
		const LevelMeshTexRange *tr = &mesh.texRanges[reg.texRangeOffset];
		for (uint32_t i = 0; i < reg.texRangeCount; i++, tr++)
		{
			if (LMAABBInFrustum(tr->aabbMin, tr->aabbMax, planes))
			{
				drawList[count].regionIndex = r;
				drawList[count].texRangeIndex = reg.texRangeOffset + i;
				count++;
			}
		}
	}
	if (drawList.Size() > count) drawList.Clamp(count);
	return count;
}

//============================================================================
//
// LevelMesh_CalcFrustumPlanes
//
// Duplicates the classic view math (the cull walk runs before RenderView
// computes VPUniforms, so those cannot be read):
//
//   view matrix  - exactly HWDrawInfo::SetViewMatrix with mult=1 /
//                  planemult = level pixelstretch: rotate roll, pitch, yaw,
//                  then translate(vx, -vz*stretch, -vy), scale(-1, stretch, 1).
//   projection   - the mono (mShiftFactor == 0) path of
//                  VREyeInfo::GetProjection: perspective(fovy, ratio, zNear,
//                  zFar) with fovy = 2*atan(tan(fov/2)/fovratio), where
//                  fov is the main view's FOV (D_Display's camera GetFOV)
//                  and ratio / fovratio come from r_viewwindow exactly like
//                  the main-view call in hw_entrypoint.cpp.
//   planes       - standard row extraction from VP = P*V (OpenGL convention,
//                  clip z in [-1, 1]): near R3+R4, far R3-R4, left R3+R1,
//                  right R3-R1, bottom R3+R2, top R3-R2.
//
//============================================================================

void LevelMesh_CalcFrustumPlanes(player_t *pl, float out[24])
{
	// No valid player/mo: all-passing planes (no culling).
	if (pl == nullptr || pl->mo == nullptr)
	{
		for (int i = 0; i < 6; i++)
		{
			out[i * 4 + 0] = out[i * 4 + 1] = out[i * 4 + 2] = 0.f;
			out[i * 4 + 3] = 1.f;
		}
		return;
	}

	AActor &mo = *pl->mo;
	const float pixelstretch = (mo.Level != nullptr && mo.Level->info != nullptr) ? mo.Level->info->pixelstretch : 1.f;

	// Position: player origin + viewz, the classic focal origin.
	const DVector3 pos = mo.Pos();
	const float vx = (float)pos.X;
	const float vy = (float)pos.Y;
	const float vz = (float)(mo.Z() + pl->viewz);

	// HW angles: same derivation as R_SetupFrame's pixelstretch-scaled pitch
	// and FRenderViewpoint::SetViewAngle's 270-degree yaw offset.
	const double radPitch = mo.Angles.Pitch.Normalized180().Radians();
	const float angx = (float)cos(radPitch);
	const float angy = (float)(sin(radPitch) * pixelstretch);
	const float alen = sqrtf(angx * angx + angy * angy);
	const float hwPitchDeg = LM_RAD2DEG((float)asin(angy / (alen > 0.f ? alen : 1.f)));
	const float hwYawDeg = 270.0f - mo.Angles.Yaw.Degrees();
	const float hwRollDeg = mo.Angles.Roll.Degrees();

	// View matrix: the classic SetViewMatrix sequence, mult = 1.
	VSMatrix view;
	view.loadIdentity();
	view.rotate(hwRollDeg, 0.0f, 0.0f, 1.0f);
	view.rotate(hwPitchDeg, 1.0f, 0.0f, 0.0f);
	view.rotate(hwYawDeg, 0.0f, -1.0f, 0.0f);
	view.translate(vx, -vz * pixelstretch, -vy);
	view.scale(-1.f, pixelstretch, 1.f);

	// Projection: the mono path of VREyeInfo::GetProjection.
	const float ratio = r_viewwindow.WidescreenRatio;
	const float fovratio = ratio >= 1.3f ? 1.333333f : ratio;
	AActor *cam = pl->camera != nullptr ? pl->camera : &mo;
	float fovDeg = (float)cam->GetFOV(I_GetTimeFrac());
	if (fovDeg < 5.f) fovDeg = 5.f;	// R_SetFOV's clamp
	else if (fovDeg > 170.f) fovDeg = 170.f;
	const float fovyDeg = (float)(2.0 * LM_RAD2DEG(atan(tan(LM_DEG2RAD(fovDeg) / 2.0) / fovratio)));

	VSMatrix proj;
	proj.perspective(fovyDeg, ratio, LM_CULL_ZNEAR, LM_CULL_ZFAR);

	// VP = P*V, then the six frustum planes from its rows. VSMatrix storage
	// is column-major: row r = (m[0*4+r], m[1*4+r], m[2*4+r], m[3*4+r]).
	VSMatrix vp;
	vp.loadMatrix(proj.get());
	vp.multMatrix(view);
	const float *m = vp.mMatrix;

	float rows[4][4];
	for (int r = 1; r <= 4; r++)
	{
		rows[r - 1][0] = m[0 * 4 + r];
		rows[r - 1][1] = m[1 * 4 + r];
		rows[r - 1][2] = m[2 * 4 + r];
		rows[r - 1][3] = m[3 * 4 + r];
	}
	const float (*r)[4] = rows;
	// R3 is the base row for all six planes; the addend row carries the sign.
	static const int addend[6] = { 3, 3, 0, 0, 1, 1 };	// near/far R4, left/right R1, bottom/top R2
	static const int sign[6] = { 1, -1, 1, -1, 1, -1 };
	for (int p = 0; p < 6; p++)
	{
		for (int i = 0; i < 4; i++)
		{
			out[p * 4 + i] = r[2][i] + sign[p] * r[addend[p]][i];
		}
	}
}

}
