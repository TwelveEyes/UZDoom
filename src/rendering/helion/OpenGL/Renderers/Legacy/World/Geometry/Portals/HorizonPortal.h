/*
** HorizonPortal.h
**
** UZDoom-only (plan M5.2). Port of HWHorizonPortal (hw_portal.h / hw_portal.cpp): the flat plane of a Line_Horizon line or an
** Eternity horizon sector portal that extends to the horizon. The geometry is generated around the viewpoint when the portal is
** created; PortalDriver::Hooks.DrawHorizon draws it with the flat material.
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

#include "Portal.h"
#include "PortalDriver.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

class HorizonPortal : public Portal
{
public:
	// info has to outlive the portal (the driver keeps the shared instances, HWPortalState::UniqueHorizons).
	HorizonPortal(const HorizonInfo* info, const FRenderViewpoint& vp);

	PortalType Type() const override { return PortalType::Horizon; }
	const void* GetSource() const override { return m_origin; }
	bool NeedDepthBuffer() const override { return false; }
	bool NeedCap() const override { return false; }
	const char* GetName() const override;
	void DrawContents(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip) override;

private:
	const HorizonInfo* m_origin;
	std::vector<HorizonVertex> m_strips;
	std::vector<HorizonVertex> m_skirt;
};

}
