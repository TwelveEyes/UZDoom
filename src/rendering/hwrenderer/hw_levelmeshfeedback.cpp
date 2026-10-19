/*
** hw_levelmeshfeedback.cpp
**
** Levelmesh GPU feedback: actor gather frustum test, exposure, out-of-body rules
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
**
** Ticket 18. The game state the classic BSP walk writes as it clips (the
** automap / savegame exposure marks SSECMF_DRAWN and ML_MAPPED, the
** FLevelLocals::cullcolor latch) comes from the previous frame's mark pass
** on the levelmesh path (ApplyLevelMeshExposure: the leaves and lines with a
** surface that passed the depth test). Out-of-body views get the classic's
** view rules as a leaf mask the levelmesh draw and the actor gather apply
** (BuildLevelMeshLeafMask / FinishLevelMeshLeafMask).
*/

// HEADER FILES ------------------------------------------------------------

#include <bit>

#include "hw_levelmeshbake.h"
#include "g_levellocals.h"
#include "actor.h"
#include "m_bbox.h"
#include "p_maputl.h"
#include "hw_clock.h"
#include "c_cvars.h"
#include "printf.h"
#include "v_video.h"

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

EXTERN_CVAR(Int, r_distance_cull_type)
EXTERN_CVAR(Bool, r_radarclipper)
EXTERN_CVAR(Bool, lm_exposurelog)

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

//==========================================================================
//
// ApplyLevelMeshExposure
//
// The classic marks a subsector SSECMF_DRAWN when one of its segs passes the
// clipper, and a line ML_MAPPED when one of its segs does (except a line
// between parts of one sector without a mid texture). Here: every leaf with
// a visible surface, every line with a visible wall or exposure quad, and
// the subsectors of the visible side's segs (polyobject sides excepted: their
// segs' subsectors are the polyobject origin's). The cullcolor latch is the
// classic's for the visible lines beyond the cull distance; of several with
// different fade colors the lowest line index wins (the classic: the last
// seg in BSP order).
//
//==========================================================================

void HWDrawInfo::ApplyLevelMeshExposure(const LevelMeshReadback &rb)
{
	auto bake = Level->levelMeshBake;
	LevelMeshExposureTime.Clock();
	const unsigned numSubsectors = Level->subsectors.Size(), numLines = Level->lines.Size();
	unsigned nleaves = 0, nlines = 0;
	// lm_exposurelog: the first newly marked subsectors (with their sector) and lines.
	FString newLeaves, newLines;
	const unsigned maxListed = 16;
	auto markSubsector = [&](subsector_t *sub)
	{
		if (sub == nullptr || (sub->flags & (SSECMF_DRAWN | SSECF_POLYORG))) return;
		sub->flags |= SSECMF_DRAWN;
		if (lm_exposurelog && nleaves < maxListed) newLeaves.AppendFormat(" %d(s%d)", sub->Index(), sub->sector ? sub->sector->Index() : -1);
		nleaves++;
	};
	for (unsigned w = 0; w < rb.leafWords; w++)
	{
		for (uint32_t m = rb.leaves[w]; m != 0; m &= m - 1)
		{
			unsigned i = w * 32 + std::countr_zero(m);
			if (i < numSubsectors) markSubsector(&Level->subsectors[i]);
		}
	}
	const bool cull = r_distance_cull_type > 0 && Viewpoint.culldistsq > 0;
	bool latched = false;
	const DVector2 eye = Viewpoint.Pos.XY();
	for (unsigned w = 0; w < rb.lineWords; w++)
	{
		for (uint32_t m = rb.lines[w]; m != 0; m &= m - 1)
		{
			unsigned i = w * 32 + std::countr_zero(m);
			if (i >= numLines) continue;
			line_t &line = Level->lines[i];
			if (!(line.flags & ML_MAPPED))
			{
				line.flags |= ML_MAPPED;
				if (lm_exposurelog && nlines < maxListed) newLines.AppendFormat(" %u", i);
				nlines++;
			}
			int sideNum = P_PointOnLineSidePrecise(eye, &line);
			if (line.sidedef[sideNum] == nullptr) sideNum = 0;
			side_t *side = line.sidedef[sideNum];
			// A side exposed here before had its segs' subsectors marked
			// then: only the cullcolor latch needs it again.
			if (i < bake->LineExposed.Size())
			{
				uint8_t bit = uint8_t(1 << sideNum);
				if ((bake->LineExposed[i] & bit) && !cull) continue;
				bake->LineExposed[i] |= bit;
			}
			if (side == nullptr || side->segs == nullptr || side->numsegs <= 0) continue;
			unsigned s = side->Index();
			if (s < bake->SidePoly.Size() && bake->SidePoly[s] >= 0) continue;
			for (int k = 0; k < side->numsegs; k++) markSubsector(side->segs[k]->Subsector);
			if (cull && !latched && IsDistanceCulled(side->segs[0]) && side->sector->Colormap.FadeColor != 0)
			{
				Level->cullcolor = side->sector->Colormap.FadeColor;
				latched = true;
			}
		}
	}
	lmExposureStats.views++;
	lmExposureStats.leaves += nleaves;
	lmExposureStats.lines += nlines;
	LevelMeshExposureTime.Unclock();
	if (lm_exposurelog)
	{
		Printf("LMEXPOSE frame %llu key %llx leaves %u lines %u cullcolor %s%06x%s%s%s%s\n", (unsigned long long)screen->FrameTime,
			(unsigned long long)lmViewKey, nleaves, nlines, latched ? "" : "kept ", (unsigned)(Level->cullcolor.d & 0xffffff),
			nleaves ? " new subsectors" : "", newLeaves.GetChars(), nlines ? " new lines" : "", newLines.GetChars());
	}
}

//==========================================================================
//
// Out-of-body view rules (ticket 18)
//
// HWDrawInfo::DoSubsector skips, in out-of-body views, the undiscovered
// secret sectors (unless r_radarclipper), the subsectors outside the radar
// (r_radarclipper: unexplored and out of the player body's sight), and in
// ortho views without the radar draws only the subsectors overlapping the
// viewbox (RenderOrthoNoFog). The levelmesh shows and gathers only the
// leaves these rules leave: the secret and viewbox rules before the walk,
// the radar rule after it from the subsectors the walk accepted
// (LevelMeshBake::RadarStamp). The classic's pitch / angle windows are
// conservative bounds of the view volume the GPU frustum already is.
//
//==========================================================================

void HWDrawInfo::BuildLevelMeshLeafMask()
{
	auto bake = Level->levelMeshBake;
	const unsigned numSubsectors = Level->subsectors.Size();
	const unsigned words = (numSubsectors + 31) / 32;
	const bool radar = r_radarclipper && !(Level->flags3 & LEVEL3_NOFOGOFWAR);
	const bool ortho = Viewpoint.bDoOrtho && ((Level->flags3 & LEVEL3_NOFOGOFWAR) || !r_radarclipper);
	const bool secrets = !r_radarclipper && bake->SecretSectors.Size() > 0;
	if (!radar && !ortho && !secrets) return;
	lmLeafMask.Resize(words);
	for (auto &w : lmLeafMask) w = ~0u;
	if (words > 0 && (numSubsectors & 31) != 0) lmLeafMask[words - 1] = (1u << (numSubsectors & 31)) - 1;
	auto clear = [&](unsigned i) { lmLeafMask[i >> 5] &= ~(1u << (i & 31)); };
	if (secrets)
	{
		for (uint32_t s : bake->SecretSectors)
		{
			if (s >= Level->sectors.Size()) continue;
			sector_t &sec = Level->sectors[s];
			if (!sec.isSecret() || !sec.wasSecret()) continue;
			for (int i = 0; i < sec.subsectorcount; i++) clear(sec.subsectors[i]->Index());
		}
	}
	if (ortho)
	{
		// RenderOrthoNoFog's viewbox.
		double ext = Viewpoint.camera->ViewPos->Offset.Length() ?
			3.0 * Viewpoint.camera->ViewPos->Offset.Length() * tan(Viewpoint.FieldOfView.Radians() * 0.5) : 100.0;
		FBoundingBox viewbox(Viewpoint.OffPos.X, Viewpoint.OffPos.Y, ext);
		for (unsigned i = 0; i < numSubsectors; i++)
			if (!Level->subsectors[i].bbox.CheckOverlap(viewbox)) clear(i);
	}
	if (radar)
	{
		// The walk stamps what it accepts with this frame (DoSubsector).
		if (++bake->RadarFrame == 0)
		{
			for (auto &v : bake->RadarStamp) v = 0;
			bake->RadarFrame = 1;
		}
	}
	lmLeafMaskValid = true;
}

void HWDrawInfo::FinishLevelMeshLeafMask()
{
	if (!lmLeafMaskValid || !r_radarclipper || (Level->flags3 & LEVEL3_NOFOGOFWAR)) return;
	auto bake = Level->levelMeshBake;
	const uint32_t frame = bake->RadarFrame;
	const unsigned n = std::min<unsigned>(bake->RadarStamp.Size(), Level->subsectors.Size());
	for (unsigned w = 0; w < lmLeafMask.Size(); w++)
	{
		for (uint32_t m = lmLeafMask[w]; m != 0; m &= m - 1)
		{
			unsigned i = w * 32 + std::countr_zero(m);
			if (i >= n || bake->RadarStamp[i] != frame) lmLeafMask[w] &= ~(1u << (i & 31));
		}
	}
}
