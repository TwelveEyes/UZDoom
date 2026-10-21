/*
** MirrorPortal.cpp
**
** UZDoom-only (plan M5.2), see MirrorPortal.h. Port of HWMirrorPortal::Setup / Shutdown.
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

#include "MirrorPortal.h"
#include "PortalDriver.h"
#include "g_levellocals.h"
#include "hw_cvars.h"
#include "r_utility.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

bool MirrorPortal::Setup(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip)
{
	PortalSceneState& state = driver.State();
	if (state.RenderDepth > r_portal_recursions)
	{
		return false;
	}

	state.VpIsAllowedOoB = vp.bDoOob;
	// This makes line mirrors work when viewed through portals
	if (m_linedef->sidedef[0] && m_linedef->sidedef[0]->numsegs > 0)
	{
		clip.MapSections.Set(m_linedef->sidedef[0]->segs[0]->Subsector->mapsection);
	}

	clip.ClipPortal = this;
	DAngle StartAngle = vp.Angles.Yaw;
	DVector3 StartPos = vp.Pos;

	vertex_t* v1 = m_linedef->v1;
	vertex_t* v2 = m_linedef->v2;

	// the player is always visible in a mirror.
	vp.showviewer = true;
	// Reflect the current view behind the mirror.
	if (m_linedef->Delta().X == 0)
	{
		// vertical mirror
		vp.Pos.X = 2 * v1->fX() - StartPos.X;

		// Compensation for reendering inaccuracies
		if (StartPos.X < v1->fX()) vp.Pos.X -= 0.1;
		else vp.Pos.X += 0.1;
	}
	else if (m_linedef->Delta().Y == 0)
	{
		// horizontal mirror
		vp.Pos.Y = 2 * v1->fY() - StartPos.Y;

		// Compensation for reendering inaccuracies
		if (StartPos.Y < v1->fY()) vp.Pos.Y -= 0.1;
		else vp.Pos.Y += 0.1;
	}
	else
	{
		// any mirror--use floats to avoid integer overflow.
		// Use doubles to avoid losing precision which is very important here.

		double dx = v2->fX() - v1->fX();
		double dy = v2->fY() - v1->fY();
		double x1 = v1->fX();
		double y1 = v1->fY();
		double x = StartPos.X;
		double y = StartPos.Y;

		// the above two cases catch len == 0
		double r = ((x - x1) * dx + (y - y1) * dy) / (dx * dx + dy * dy);

		vp.Pos.X = (x1 + r * dx) * 2 - x;
		vp.Pos.Y = (y1 + r * dy) * 2 - y;

		// Compensation for reendering inaccuracies
		FVector2 v(-dx, dy);
		v.MakeUnit();

		vp.Pos.X += v[1] * state.RenderDepth / 2;
		vp.Pos.Y += v[0] * state.RenderDepth / 2;
	}
	vp.Angles.Yaw = m_linedef->Delta().Angle() * 2. - StartAngle;

	vp.ViewActor = nullptr;

	state.MirrorFlag++;

	// di->SetClipLine(linedef)
	clip.Kind = PortalClipKind::Line;
	clip.ClipLine[0] = (float)m_linedef->v1->fX();
	clip.ClipLine[1] = (float)m_linedef->v1->fY();
	clip.ClipLine[2] = (float)m_linedef->Delta().X;
	clip.ClipLine[3] = (float)m_linedef->Delta().Y;

	// di->SetupView
	vp.SetViewAngle(r_viewwindow);

	// clipper->Clear(), then the frustum of the view
	ClipOutsideFrustum(vp, clip);

	// clipper->SafeAddClipRange(v1, v2): everything but the counter clockwise range from the angle of v2 to the angle of v1 (the
	// opening of the mirror seen from behind it) is hidden.
	if (!vp.bDoOob)
	{
		constexpr uint64_t FullCircle = 0x100000000ull;
		uint64_t from = (m_linedef->v2->fPos() - vp.Pos.XY()).Angle().BAMs();
		uint64_t span = (uint32_t)((m_linedef->v1->fPos() - vp.Pos.XY()).Angle().BAMs() - (uint32_t)from);
		if (from + span <= FullCircle)
			clip.AddBlockedBam(from, from + span);
		else
		{
			clip.AddBlockedBam(from, FullCircle);
			clip.AddBlockedBam(0, from + span - FullCircle);
		}
	}
	return true;
}

void MirrorPortal::Shutdown(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp)
{
	driver.State().MirrorFlag--;
}

const char* MirrorPortal::GetName() const
{
	return "Mirror";
}

}
