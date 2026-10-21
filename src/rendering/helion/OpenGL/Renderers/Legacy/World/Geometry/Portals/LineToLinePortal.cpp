/*
** LineToLinePortal.cpp
**
** UZDoom-only (plan M5.2), see LineToLinePortal.h. Port of HWLineToLinePortal::Setup / RenderAttached.
**
**---------------------------------------------------------------------------
**
** Copyright 2004-2018 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include "LineToLinePortal.h"
#include "PortalDriver.h"
#include "actor.h"
#include "g_levellocals.h"
#include "hw_cvars.h"
#include "p_maputl.h"
#include "portal.h"
#include "r_utility.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

bool LineToLinePortal::Setup(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip)
{
	// TODO: Handle recursion more intelligently
	PortalSceneState& state = driver.State();
	if (state.RenderDepth > r_portal_recursions)
	{
		return false;
	}
	state.VpIsAllowedOoB = vp.bDoOob;
	clip.ClipPortal = this;

	line_t* origin = m_glport->lines[0]->mOrigin;
	P_TranslatePortalXY(origin, vp.Pos.X, vp.Pos.Y);
	P_TranslatePortalXY(origin, vp.ActorPos.X, vp.ActorPos.Y);
	P_TranslatePortalAngle(origin, vp.Angles.Yaw);
	P_TranslatePortalZ(origin, vp.Pos.Z);
	P_TranslatePortalXY(origin, vp.Path[0].X, vp.Path[0].Y);
	P_TranslatePortalXY(origin, vp.Path[1].X, vp.Path[1].Y);
	P_TranslatePortalXY(origin, vp.OffPos.X, vp.OffPos.Y);
	P_TranslatePortalZ(origin, vp.OffPos.Z);
	if (!vp.showviewer && vp.camera != nullptr && P_PointOnLineSidePrecise(vp.Path[0], m_glport->lines[0]->mDestination) != P_PointOnLineSidePrecise(vp.Path[1], m_glport->lines[0]->mDestination))
	{
		double distp = (vp.Path[0] - vp.Path[1]).Length();
		if (distp > EQUAL_EPSILON)
		{
			double dist1 = (vp.Pos - vp.Path[0]).Length();
			double dist2 = (vp.Pos - vp.Path[1]).Length();

			if (dist1 + dist2 < distp + 1)
			{
				// Restored by Portal::RemoveStencil (savedvisibility).
				vp.camera->renderflags |= RF_MAYBEINVISIBLE;
			}
		}
	}

	FLevelLocals* level = driver.Level();
	for (unsigned i = 0; i < Lines.size(); i++)
	{
		line_t* line = Lines[i].Seg->linedef->getPortalDestination();
		subsector_t* sub;
		if (line->sidedef[0]->Flags & WALLF_POLYOBJ)
			sub = level->PointInRenderSubsector(line->v1->fixX(), line->v1->fixY());
		else sub = line->frontsector->subsectors[0];
		clip.MapSections.Set(sub->mapsection);
	}

	vp.ViewActor = nullptr;

	// di->SetClipLine(glport->lines[0]->mDestination)
	line_t* destination = m_glport->lines[0]->mDestination;
	clip.Kind = PortalClipKind::Line;
	clip.ClipLine[0] = (float)destination->v1->fX();
	clip.ClipLine[1] = (float)destination->v1->fY();
	clip.ClipLine[2] = (float)destination->Delta().X;
	clip.ClipLine[3] = (float)destination->Delta().Y;

	// di->SetupView
	vp.SetViewAngle(r_viewwindow);

	ClearClipper(outer, vp, clip);
	return true;
}

void LineToLinePortal::RenderAttached(PortalDriver& driver, const PortalFrameView& view)
{
	if (driver.Hooks.ProcessActorsInPortal)
		driver.Hooks.ProcessActorsInPortal(m_glport);
}

const char* LineToLinePortal::GetName() const
{
	return "LineToLine";
}

}
