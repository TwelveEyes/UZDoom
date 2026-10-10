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
// z-slices (z=0 and z=LM_CULL_ZMAX) in DOOM space, giving eight corner points.
// The frustum planes are in GPU space - levelmesh.vp transforms vertices as
// worldpos = (x, z_doom, y_doom) before ViewMatrix - so the plane dot uses
// pl[1] with the DOOM z and pl[2] with the DOOM y. The entry
// is kept iff ANY of the eight corners is inside ALL six frustum planes;
// it is culled only if EVERY corner fails at least one plane. For a convex
// box this is exact: all-corners-outside implies the whole box is outside,
// so the test can only ever cull - never keep a box that pokes in. The
// z=0 / z=zMax extrusion is conservative (real walls span between sector
// heights), which keeps visible geometry; it only weakens culling for
// boxes whose real height range is much smaller.
//
//============================================================================

// Clip-space corner test: a point is inside the frustum when its clip coords
// satisfy |x| <= w, |y| <= w and -w <= z <= w.  This uses the exact same matrix
// product as the vertex shader (uProj * uView), so no plane-extraction or layout
// assumptions are needed.
static bool LMPointInFrustum(VSMatrix &vp, float x, float y, float z)
{
	float p[4] = { x, y, z, 1.f };
	float o[4];
	vp.multMatrixPoint(p, o);
	const float w = o[3];
	if (w <= 0.f) return false; // behind the camera
	return fabsf(o[0]) <= w && fabsf(o[1]) <= w && o[2] >= -w && o[2] <= w;
}

static bool LMAABBInFrustum(const float aabbMin[2], const float aabbMax[2], VSMatrix &vp)
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
				if (LMPointInFrustum(vp, xs[ix], ys[iy], zs[iz])) return true;
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

uint32_t FLevelMeshFrame::Build(const FLevelMesh &mesh, VSMatrix &vp)
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
			if (LMAABBInFrustum(tr->aabbMin, tr->aabbMax, vp))
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
// LevelMesh_CalcCullVP
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
//
// Culling then tests texRange AABB corners through this VP in clip space, the
// exact same product as the vertex shader - no plane extraction needed.
//
//============================================================================

void LevelMesh_CalcCullVP(player_t *pl, VSMatrix &vp)
{
	if (pl == nullptr || pl->mo == nullptr)
	{
		vp.loadIdentity(); // identity VP passes everything in clip space only for the origin; culling stays off via the caller's early-out
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

	vp.loadMatrix(proj.get());
	vp.multMatrix(view);
}

}
