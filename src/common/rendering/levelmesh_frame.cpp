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
// LM_CULL_ZMAX bounds the DOOM-z extrusion of the 2D sub-range AABBs into
// 3D for the plane test (see LMAABBInFrustum): [-ZMAX, ZMAX] covers the
// full fixed-point height range, so no geometry can fall outside the box.
//
//============================================================================

static constexpr float LM_CULL_ZNEAR = 5.f;
static constexpr float LM_CULL_ZFAR = 65536.f;
static constexpr float LM_CULL_ZMAX = 32768.f;

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
// The conservative cull test: a sub-range entry's 2D world AABB is extruded
// to z in [-LM_CULL_ZMAX, LM_CULL_ZMAX] (DOOM space), giving eight corners.
// The corners are transformed with the same VP product as the vertex shader,
// in its GPU space: levelmesh.vp uses worldpos = (x, z_doom, y_doom).
//
// The box is culled only when all eight corners lie outside the SAME clip
// plane (x < -w, x > w, y < -w, y > w, z < -w, z > w). Each inequality is
// linear in the homogeneous clip coordinates, i.e. a half-space in world
// space, so all-corners-outside-one-plane proves the whole box is outside -
// this also holds for corners behind the camera (w <= 0). Testing whether
// any single corner is inside is NOT conservative: a wall crossing the view
// with both ends off-screen has every corner outside yet is visible.
//
//============================================================================

static bool LMAABBInFrustum(const float aabbMin[2], const float aabbMax[2], VSMatrix &vp)
{
	const float xs[2] = { aabbMin[0], aabbMax[0] };
	const float ys[2] = { aabbMin[1], aabbMax[1] };
	const float zs[2] = { -LM_CULL_ZMAX, LM_CULL_ZMAX };

	// One bit per clip plane; a corner clears the bits of the planes it is
	// inside of. Any bit still set after all corners = all outside that plane.
	int allOutside = 0x3f;
	for (int ix = 0; ix < 2; ix++)
	{
		for (int iy = 0; iy < 2; iy++)
		{
			for (int iz = 0; iz < 2; iz++)
			{
				float p[4] = { xs[ix], zs[iz], ys[iy], 1.f };	// GPU space (x, z, y)
				float o[4];
				vp.multMatrixPoint(p, o);
				const float w = o[3];
				int out = 0;
				if (o[0] < -w) out |= 1;
				if (o[0] > w) out |= 2;
				if (o[1] < -w) out |= 4;
				if (o[1] > w) out |= 8;
				if (o[2] < -w) out |= 16;
				if (o[2] > w) out |= 32;
				allOutside &= out;
				if (allOutside == 0) return true;
			}
		}
	}
	return allOutside == 0;
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
	// The previous Build (or the previous view's re-cull) clamped Count to its
	// survivor count; restore the full size before writing, or every later
	// build stays capped at the smallest count ever seen (a single fully culled
	// view would black out the level). Capacity >= bound, so no allocation.
	drawList.Resize(bound);

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
	const float vz = (float)pl->viewz;	// player_t::viewz is absolute (P_CalcHeight)

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
	view.rotate(hwYawDeg, 0.0f, 1.0f, 0.0f);	// SetViewMatrix: axis (0, mult, 0), mult = 1 (no mirror)
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
