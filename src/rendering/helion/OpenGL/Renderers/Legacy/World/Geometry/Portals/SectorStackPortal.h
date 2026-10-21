/*
** SectorStackPortal.h
**
** UZDoom-only (plan M5.2). Port of HWSectorStackPortal (hw_portal.h / hw_portal.cpp): the view through a sector plane portal
** (stacked sector things, Sector_SetPortal, linked portals) is the world seen from the viewpoint displaced by the portal group.
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

#pragma once

#include "ScenePortal.h"

struct FSectorPortalGroup;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

class SectorStackPortal : public ScenePortal
{
public:
	// The subsectors of the view that have the portal group on their ceiling / floor (classic AddSubsectorToPortal).
	std::vector<subsector_t*> Subsectors;

	explicit SectorStackPortal(FSectorPortalGroup* group) : m_origin(group)
	{
	}

	PortalType Type() const override { return PortalType::SectorStack; }
	const void* GetSource() const override { return m_origin; }
	// although this isn't a real sky it can be handled as one.
	bool IsSky() const override { return true; }
	const char* GetName() const override;

	void AddSubsector(subsector_t* sub) { Subsectors.push_back(sub); }
	void DrawPortalStencil(PortalDriver& driver, const PortalFrameView& outer, int pass) override;

protected:
	bool Setup(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip) override;
	void Shutdown(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp) override;

private:
	FSectorPortalGroup* m_origin;
	StencilRange m_flatRange;
	bool m_flatBuilt = false;

	void SetupCoverage(FLevelLocals* level, PortalClip& clip) const;
};

}
