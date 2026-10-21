/*
** EEHorizonPortal.cpp
**
** UZDoom-only (plan M5.2), see EEHorizonPortal.h. Port of HWEEHorizonPortal::DrawContents.
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

#include "EEHorizonPortal.h"
#include "HorizonPortal.h"
#include "PortalDriver.h"
#include "g_levellocals.h"
#include "portal.h"
#include "r_utility.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

void EEHorizonPortal::DrawContents(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip)
{
	sector_t* sector = m_portal->mOrigin;
	if (sector->GetTexture(sector_t::floor) == skyflatnum ||
		sector->GetTexture(sector_t::ceiling) == skyflatnum)
	{
		// HWSkyInfo::init(di, sector, ceiling, sector->skytransfer, 0) + HWSkyPortal::DrawContents
		driver.UnbindProgram();
		if (driver.Hooks.DrawSectorSky)
			driver.Hooks.DrawSectorSky(sector, sector_t::ceiling, vp);
	}
	if (sector->GetTexture(sector_t::ceiling) != skyflatnum)
	{
		HorizonInfo horz;
		horz.Plane.GetFromSector(sector, sector_t::ceiling);
		horz.LightLevel = sector->GetCeilingLight();
		horz.Colormap = sector->Colormap;
		horz.SpecialColor = 0xffffffff;
		if (m_portal->mType == PORTS_PLANE)
		{
			horz.Plane.Texheight = vp.Pos.Z + fabs(horz.Plane.Texheight);
		}
		HorizonPortal ceil(&horz, vp);
		ceil.DrawContents(driver, outer, vp, clip);
	}
	if (sector->GetTexture(sector_t::floor) != skyflatnum)
	{
		HorizonInfo horz;
		horz.Plane.GetFromSector(sector, sector_t::floor);
		horz.LightLevel = sector->GetFloorLight();
		horz.Colormap = sector->Colormap;
		horz.SpecialColor = 0xffffffff;
		if (m_portal->mType == PORTS_PLANE)
		{
			horz.Plane.Texheight = vp.Pos.Z - fabs(horz.Plane.Texheight);
		}
		HorizonPortal floor(&horz, vp);
		floor.DrawContents(driver, outer, vp, clip);
	}
}

const char* EEHorizonPortal::GetName() const
{
	return "EEHorizon";
}

}
