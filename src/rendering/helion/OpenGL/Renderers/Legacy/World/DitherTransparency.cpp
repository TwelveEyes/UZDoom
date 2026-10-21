/*
** DitherTransparency.cpp
**
** UZDoom-only: r_dithertransparency for out of bounds views (classic SetDitherTransFlags).
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
#include "OpenGL/Renderers/Legacy/World/DitherTransparency.h"
#include "g_levellocals.h"
#include "actor.h"
#include "d_player.h"
#include "p_local.h"
#include "p_trace.h"
#include "r_utility.h"
#include "hw_cvars.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

namespace
{

struct TraceContext
{
	DitherTransparency* Dither;
	int MapSection;
};

bool InMapSection(sector_t* sector, int mapSection)
{
	return sector->subsectorcount > 0 && sector->subsectors[0]->mapsection == mapSection;
}

void FlagPlane(TraceContext& context, secplane_t* plane)
{
	if (!plane->dithertransflag)
	{
		plane->dithertransflag = true;
		context.Dither->Flagged(plane);
	}
}

//==========================================================================
//
// TraceCallbackForDitherTransparency
// Toggles dither flag on anything that occludes the actor's
// position from viewpoint.
//
//==========================================================================

ETraceStatus TraceCallbackForDitherTransparency(FTraceResults& res, void* userdata)
{
	TraceContext& context = *(TraceContext*)userdata;
	double bf, bc;

	switch(res.HitType)
	{
	case TRACE_HitWall:
		{
			side_t* side = res.Line->sidedef[res.Side];
			sector_t* linesec = side->sector;
			if (InMapSection(linesec, context.MapSection))
			{
				bf = linesec->floorplane.ZatPoint(res.HitPos.XY());
				bc = linesec->ceilingplane.ZatPoint(res.HitPos.XY());
				if (res.Line->sidedef[!res.Side])
				{
					// Two sided line! So let's find out if mid, top, or bottom texture needs dithered transparency
					bf = max(bf, res.Line->sidedef[!res.Side]->sector->floorplane.ZatPoint(res.HitPos.XY()));
					bc = min(bc, res.Line->sidedef[!res.Side]->sector->ceilingplane.ZatPoint(res.HitPos.XY()));
					int tier = res.HitPos.Z <= bf ? side_t::bottom : res.HitPos.Z < bc ? side_t::mid : side_t::top;
					side->Flags |= WALLF_DITHERTRANS << tier;
					side->dithertranscount = max<int>(1, res.Line->sidedef[!res.Side]->sector->e->XFloor.ffloors.Size());
					context.Dither->Flagged(side);
				}
				else if ((res.HitPos.Z <= bc) && (res.HitPos.Z >= bf))
				{
					side->Flags |= WALLF_DITHERTRANS_MID;
					side->dithertranscount = 1;
					context.Dither->Flagged(side);
				}
			}
		}
		break;
	case TRACE_HitFloor:
		if (InMapSection(res.Sector, context.MapSection) && res.HitVector.dot(res.Sector->floorplane.Normal()) < 0.0)
		{
			if (res.HitPos.Z == res.Sector->floorplane.ZatPoint(res.HitPos))
			{
				FlagPlane(context, &res.Sector->floorplane);
			}
			else if (res.Sector->e->XFloor.ffloors.Size()) // Maybe it was 3D floors
			{
				for (F3DFloor* rover : res.Sector->e->XFloor.ffloors)
				{
					if ((rover->flags&(FF_EXISTS | FF_RENDERPLANES | FF_THISINSIDE)) == (FF_EXISTS | FF_RENDERPLANES))
					{
						if (res.HitPos.Z == rover->top.plane->ZatPoint(res.HitPos))
						{
							FlagPlane(context, rover->top.plane);
							break; // Out of for loop
						}
					}
				}
			}
		}
		break;
	case TRACE_HitCeiling:
		if (InMapSection(res.Sector, context.MapSection) && res.HitVector.dot(res.Sector->ceilingplane.Normal()) < 0.0)
		{
			if (res.HitPos.Z == res.Sector->ceilingplane.ZatPoint(res.HitPos))
			{
				FlagPlane(context, &res.Sector->ceilingplane);
			}
			else if (res.Sector->e->XFloor.ffloors.Size()) // Maybe it was 3D floors
			{
				for (F3DFloor* rover : res.Sector->e->XFloor.ffloors)
				{
					if ((rover->flags&(FF_EXISTS | FF_RENDERPLANES | FF_THISINSIDE)) == (FF_EXISTS | FF_RENDERPLANES))
					{
						if (res.HitPos.Z == rover->bottom.plane->ZatPoint(res.HitPos))
						{
							FlagPlane(context, rover->bottom.plane);
							break; // Out of for loop
						}
					}
				}
			}
		}
		break;
	case TRACE_HitActor:
	default:
		break;
	}

	return TRACE_ContinueOutOfBounds;
}

}

bool DitherTransparency::Enabled(const FRenderViewpoint& vp)
{
	return r_dithertransparency && vp.bDoOob;
}

bool DitherTransparency::SideDithered(const side_t* side, int tier)
{
	// HWWall::RenderWall: the tier's own flag, or the mid flag for any tier.
	return (side->Flags & ((WALLF_DITHERTRANS << tier) | WALLF_DITHERTRANS_MID)) != 0;
}

void DitherTransparency::BeginWalk(bool active)
{
	m_active = active;
	m_targets.clear();
}

void DitherTransparency::CollectTargets(sector_t* sector)
{
	if (!m_active)
		return;

	for (auto p = sector->touching_renderthings; p != nullptr && m_targets.size() < MaxTargets; p = p->m_snext)
	{
		AActor* thing = p->m_thing;
		if (((thing->flags3 & MF3_ISMONSTER) && !(thing->flags & MF_CORPSE)) || (thing->flags & MF_MISSILE))
		{
			if (std::find(m_targets.begin(), m_targets.end(), thing) == m_targets.end()) // Don't double count
				m_targets.push_back(thing);
		}
	}
}

void DitherTransparency::EndWalk(FLevelLocals* level, const FRenderViewpoint& vp)
{
	// The flags are owned by the walk that set them (the classic renderer clears them when it draws the surface).
	for (side_t* side : m_flaggedSides)
	{
		side->Flags &= ~(WALLF_DITHERTRANS_TOP | WALLF_DITHERTRANS_MID | WALLF_DITHERTRANS_BOTTOM);
		side->dithertranscount = 0;
	}
	for (secplane_t* plane : m_flaggedPlanes)
		plane->dithertransflag = false;
	m_flaggedSides.clear();
	m_flaggedPlanes.clear();

	if (!m_active)
		return;

	// classic UpdateCurrentMapSection: the map section the out of bounds view is in.
	int mapSection = level->PointInRenderSubsector(vp.OffPos.XY())->mapsection;

	SetFlags(level, vp, vp.camera->tracer ? vp.camera->tracer.Get() : players[consoleplayer].mo, mapSection);

	// Make rendered targets set dither transparency flags on level geometry for next pass
	for (AActor* target : m_targets)
	{
		if (P_CheckSight(players[consoleplayer].mo, target, 0))
			SetFlags(level, vp, target, mapSection);
	}
	m_targets.clear();
}

void DitherTransparency::SetFlags(FLevelLocals* level, const FRenderViewpoint& vp, AActor* actor, int mapSection)
{
	// This should really be moved to a shader and have the GPU do some shape-tracing.
	if (actor && actor->Sector)
	{
		TraceContext context = { this, mapSection };
		FTraceResults results;
		double horix = vp.Sin * actor->radius;
		double horiy = vp.Cos * actor->radius;
		DVector3 actorpos = actor->Pos();
		DVector3 vvec = actorpos - vp.Pos;
		if (vp.bDoOrtho)
		{
			vvec = 5.0 * vp.camera->ViewPos->Offset.Length() * vp.ViewVector3D; // Should be 4.0? (since zNear is behind screen by 3*dist in VREyeInfo::GetProjection())
		}
		double distance = vvec.Length() - actor->radius;
		DVector3 campos = actorpos - vvec;
		sector_t* startsec;

		vvec = vvec.Unit();
		campos.X -= horix; campos.Y += horiy; campos.Z += actor->Height * 0.25;
		for (int iter = 0; iter < 3; iter++)
		{
			startsec = level->PointInRenderSubsector(campos)->sector;
			Trace(campos, startsec, vvec, distance,
				  0, 0, actor, results, TRACE_PortalRestrict, TraceCallbackForDitherTransparency, &context);
			campos.Z += actor->Height * 0.5;
			Trace(campos, startsec, vvec, distance,
				  0, 0, actor, results, TRACE_PortalRestrict, TraceCallbackForDitherTransparency, &context);
			campos.Z -= actor->Height * 0.5;
			campos.X += horix; campos.Y -= horiy;
		}

		// Tracers don't work on 3D floors when you are starting in the same sector (standing under them, for example)
		for (F3DFloor* rover : actor->Sector->e->XFloor.ffloors)
		{
			FlagPlane(context, rover->top.plane);
			FlagPlane(context, rover->bottom.plane);
		}
	}
}

}
