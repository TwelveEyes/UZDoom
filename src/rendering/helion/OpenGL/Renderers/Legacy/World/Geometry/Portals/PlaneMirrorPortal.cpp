/*
** PlaneMirrorPortal.cpp
**
** UZDoom-only (plan M5.2), see PlaneMirrorPortal.h. Port of HWPlaneMirrorPortal.
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

#include "PlaneMirrorPortal.h"
#include <set>
#include "PortalDriver.h"
#include "g_levellocals.h"
#include "hw_cvars.h"
#include "r_utility.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

void PlaneMirrorPortal::SetupCoverage(FLevelLocals* level, PortalClip& clip) const
{
	InitCoverage(level, clip);
	for (unsigned int i = 0; i < Lines.size(); i++)
	{
		subsector_t* sub = Lines[i].Sub;
		clip.MapSections.Set(sub->mapsection);
		clip.CoveredSubsectors.Set(sub->Index());
	}
	SetCoverage(level, level->HeadNode(), clip);
}

bool PlaneMirrorPortal::Setup(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip)
{
	PortalSceneState& state = driver.State();
	if (state.RenderDepth > r_portal_recursions)
	{
		return false;
	}
	// A plane mirror needs to flip the portal exclusion logic because inside the mirror, up is down and down is up.
	std::swap(screen->instack[sector_t::floor], screen->instack[sector_t::ceiling]);

	state.VpIsAllowedOoB = vp.bDoOob;
	m_oldPlaneMirrorMode = state.PlaneMirrorMode;

	// the player is always visible in a mirror.
	vp.showviewer = true;

	double planez = m_origin->ZatPoint(vp.Pos);
	vp.Pos.Z = 2 * planez - vp.Pos.Z;
	vp.OffPos.Z = 2 * planez - vp.OffPos.Z;
	vp.ViewVector3D.Z = -vp.ViewVector3D.Z;
	vp.ViewActor = nullptr;
	state.PlaneMirrorMode = m_origin->fC() < 0 ? -1 : 1;

	state.PlaneMirrorFlag++;
	// di->SetClipHeight
	clip.Kind = PortalClipKind::Height;
	clip.ClipHeight = (float)planez;
	clip.ClipHeightDirection = state.PlaneMirrorMode < 0 ? -1.f : 1.f;

	// di->SetupView (recomputes the view vector, which is then flipped for the mirror)
	vp.SetViewAngle(r_viewwindow);
	vp.ViewVector3D.Z = -vp.ViewVector3D.Z;
	if (outer.CurrentPortal && outer.CurrentPortal->Type() != PortalType::PlaneMirror)
	{
		SetupCoverage(driver.Level(), clip);
	}
	ClearClipper(outer, vp, clip);

	UpdateCurrentMapSection(driver.Level(), vp, clip);
	return true;
}

void PlaneMirrorPortal::BuildGeometry(PortalDriver& driver, const PortalFrameView& outer)
{
	m_geometryBuilt = true;

	bool isceiling = (PlanesUsed & (1 << sector_t::ceiling)) != 0;
	int plane = isceiling ? sector_t::ceiling : sector_t::floor;
	float side = (float)GetMirrorSide();
	// HWPortal::SetupStencil translates the view by -zshift * side in stretched units.
	float shift = -ZShift * side / outer.PixelStretch;

	m_flatRange.First = driver.StencilCount();
	std::set<FSection*> sections;
	for (const PortalSurface& line : Lines)
	{
		if (line.Sub->section == nullptr || !sections.insert(line.Sub->section).second)
			continue;
		AddFlatStencil(driver, line.Sub->section, line.Sub->sector, plane, shift);
	}
	m_flatRange.Count = driver.StencilCount() - m_flatRange.First;

	m_edgeRange.First = driver.StencilCount();
	for (unsigned int i = 0; i < Lines.size(); i++)
	{
		const PortalSurface& line = Lines[i];
		bool edgeline = (i == 0) || !line.Seg->backsector || line.Seg->backsector->isClosed()
			|| line.Seg->frontsector->isClosed()
			|| (line.Seg->frontsector->reflect[plane] != line.Seg->backsector->reflect[plane]);
		if (!edgeline && line.Seg->backsector)
		{
			if (isceiling)
			{
				edgeline |= line.Seg->frontsector->ceilingplane.fD() != line.Seg->backsector->ceilingplane.fD();
			}
			else
			{
				edgeline |= line.Seg->frontsector->floorplane.fD() != line.Seg->backsector->floorplane.fD();
			}
		}
		if (edgeline)
		{
			const secplane_t& secplane = line.Sub->sector->GetSecPlane(plane);
			DVector2 p0 = line.Vertexes[0]->fPos();
			DVector2 p1 = line.Vertexes[1]->fPos();
			float z0 = (float)secplane.ZatPoint(p0);
			float z1 = (float)secplane.ZatPoint(p1);
			float up = ZShift * side;
			const float a[3] = { (float)p0.X, (float)p0.Y, z0 + shift };
			const float b[3] = { (float)p1.X, (float)p1.Y, z1 + shift };
			const float c[3] = { (float)p1.X, (float)p1.Y, z1 + up + shift };
			const float d[3] = { (float)p0.X, (float)p0.Y, z0 + up + shift };
			driver.AddStencilQuad(a, b, c, d);
		}
	}
	m_edgeRange.Count = driver.StencilCount() - m_edgeRange.First;
}

void PlaneMirrorPortal::DrawPortalStencil(PortalDriver& driver, const PortalFrameView& outer, int pass)
{
	if (!m_geometryBuilt)
		BuildGeometry(driver, outer);

	driver.BeginStencilDraw(outer);
	if (m_flatRange.Count > 0)
		driver.DrawStencilRange(m_flatRange.First, m_flatRange.Count);
	// Cannot combine these two: the flat is drawn before the edge strip in the classic renderer.
	if (m_edgeRange.Count > 0)
		driver.DrawStencilRange(m_edgeRange.First, m_edgeRange.Count);
	driver.EndStencilDraw(outer);
}

void PlaneMirrorPortal::Shutdown(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp)
{
	PortalSceneState& state = driver.State();
	state.PlaneMirrorFlag--;
	state.PlaneMirrorMode = m_oldPlaneMirrorMode;
	std::swap(screen->instack[sector_t::floor], screen->instack[sector_t::ceiling]);
}

const char* PlaneMirrorPortal::GetName() const
{
	return m_origin->fC() < 0 ? "Planemirror ceiling" : "Planemirror floor";
}

int PlaneMirrorPortal::GetMirrorSide() const
{
	return m_origin->fC() < 0 ? -1 : 1;
}

}
