/*
** SkyboxPortal.h
**
** UZDoom-only (plan M5.2). Port of HWSkyboxPortal (hw_portal.h / hw_portal.cpp): a sector portal of type PORTS_SKYVIEWPOINT. The view
** through it is the world seen from the skybox actor (sector_t::GetPortal(plane)->mSkybox). Honours gl_noskyboxes (the collection
** step leaves such planes to the level sky) and the recursion limit of three nested skyboxes.
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

struct FSectorPortal;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

class SkyboxPortal : public ScenePortal
{
public:
	explicit SkyboxPortal(FSectorPortal* portal) : m_portal(portal)
	{
	}

	PortalType Type() const override { return PortalType::Skybox; }
	const void* GetSource() const override { return m_portal; }
	bool IsSky() const override { return true; }
	const char* GetName() const override;
	// [MK] sector skyboxes don't allow SSAO by default
	bool AllowSSAO() const override { return false; }

protected:
	bool Setup(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip) override;
	void Shutdown(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp) override;

private:
	bool m_oldClamp = false;
	int m_oldPlaneMirrorMode = 0;
	FSectorPortal* m_portal;
};

}
