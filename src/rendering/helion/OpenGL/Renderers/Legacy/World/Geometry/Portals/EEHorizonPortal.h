/*
** EEHorizonPortal.h
**
** UZDoom-only (plan M5.2). Port of HWEEHorizonPortal (hw_portal.h / hw_portal.cpp): an Eternity style horizon / plane sector portal.
** To the rest of the engine these masquerade as a skybox portal; internally they draw the level sky and the horizon planes of the
** sector with the horizon classes.
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

struct FSectorPortal;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

class EEHorizonPortal : public Portal
{
public:
	explicit EEHorizonPortal(FSectorPortal* portal) : m_portal(portal)
	{
	}

	// Classic HWEEHorizonPortal::GetHWPortalType() is HWP_HORIZON.
	PortalType Type() const override { return PortalType::Horizon; }
	const void* GetSource() const override { return m_portal; }
	bool NeedDepthBuffer() const override { return false; }
	bool NeedCap() const override { return false; }
	const char* GetName() const override;
	void DrawContents(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip) override;

private:
	FSectorPortal* m_portal;
};

}
