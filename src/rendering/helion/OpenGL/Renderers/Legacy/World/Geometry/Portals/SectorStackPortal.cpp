/*
** SectorStackPortal.cpp
**
** UZDoom-only (plan M5.2), see SectorStackPortal.h. Port of HWSectorStackPortal.
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

#include "SectorStackPortal.h"
#include <set>
#include "PortalDriver.h"
#include "g_levellocals.h"
#include "portal.h"
#include "r_utility.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

void SectorStackPortal::SetupCoverage(FLevelLocals* level, PortalClip& clip) const
{
	InitCoverage(level, clip);
	for (unsigned i = 0; i < Subsectors.size(); i++)
	{
		subsector_t* sub = Subsectors[i];
		int plane = m_origin->plane;
		for (int j = 0; j < sub->portalcoverage[plane].sscount; j++)
		{
			subsector_t* dsub = &level->subsectors[sub->portalcoverage[plane].subsectors[j]];
			clip.MapSections.Set(dsub->mapsection);
			clip.CoveredSubsectors.Set(dsub->Index());
		}
	}
	SetCoverage(level, level->HeadNode(), clip);
}

bool SectorStackPortal::Setup(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip)
{
	PortalSceneState& state = driver.State();
	if (state.RenderDepth > 100) // energency abort in case a map manages to set up a recursion.
	{
		return false;
	}

	FLevelLocals* level = driver.Level();
	state.VpIsAllowedOoB = vp.bDoOob;

	vp.Pos += m_origin->mDisplacement;
	vp.ActorPos += m_origin->mDisplacement;
	vp.ViewActor = nullptr;
	vp.OffPos += m_origin->mDisplacement;

	// avoid recursions!
	if (m_origin->plane != -1) screen->instack[m_origin->plane]++;
	if (vp.bDoOob && Lines.size() > 0)
	{
		const secplane_t& plane = Lines[0].Sub->sector->GetSecPlane(m_origin->plane);
		clip.Kind = PortalClipKind::Height;
		clip.ClipHeight = (float)plane.ZatPoint(vp.Pos);
		clip.ClipHeightDirection = plane.Normal().Z > 0 ? -1.f : 1.f;
	}
	// di->SetupView
	vp.SetViewAngle(r_viewwindow);
	SetupCoverage(level, clip);
	ClearClipper(outer, vp, clip);

	// If the viewpoint is not within the portal, we need to invalidate the entire clip area.
	// The portal will re-validate the necessary parts when its subsectors get traversed.
	subsector_t* sub = level->PointInRenderSubsector(vp.Pos);
	if (vp.bDoOob) sub = level->PointInRenderSubsector(vp.OffPos);
	if (!clip.CoveredSubsectors[sub->Index()])
	{
		clip.AddBlockedBam(0, 0x100000000ull);
		clip.BlockedUnlessCovered = true;
	}
	return true;
}

void SectorStackPortal::DrawPortalStencil(PortalDriver& driver, const PortalFrameView& outer, int pass)
{
	if (driver.State().VpIsAllowedOoB)
	{
		if (!m_flatBuilt)
		{
			m_flatBuilt = true;
			bool isceiling = (PlanesUsed & (1 << sector_t::ceiling)) != 0;
			m_flatRange.First = driver.StencilCount();
			std::set<FSection*> sections;
			for (subsector_t* sub : Subsectors)
			{
				if (sub->section == nullptr || !sections.insert(sub->section).second)
					continue;
				AddFlatStencil(driver, sub->section, sub->sector, isceiling ? sector_t::ceiling : sector_t::floor, 0.0f);
			}
			m_flatRange.Count = driver.StencilCount() - m_flatRange.First;
		}

		driver.BeginStencilDraw(outer);
		if (m_flatRange.Count > 0)
			driver.DrawStencilRange(m_flatRange.First, m_flatRange.Count);
		driver.EndStencilDraw(outer);
	}
	else
	{
		Portal::DrawPortalStencil(driver, outer, pass);
	}
}

void SectorStackPortal::Shutdown(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp)
{
	if (m_origin->plane != -1) screen->instack[m_origin->plane]--;
}

const char* SectorStackPortal::GetName() const
{
	return "Sectorstack";
}

}
