/*
** PlaneMirrorPortal.h
**
** UZDoom-only (plan M5.2). Port of HWPlaneMirrorPortal (hw_portal.h / hw_portal.cpp): a reflective floor or ceiling (sector_t::reflect).
** The view through it is the world seen from the viewpoint mirrored at the plane, upside down.
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
** The stencil geometry is the flat itself plus a strip along the border edges. The classic renderer shifts the view matrix by
** zshift for the stencil passes so that the mask does not z-fight with the flats; here the shift is baked into the vertices
** (height - ZShift * side / pixel stretch, the shift of HWPortal::SetupStencil expressed in map units).
**
*/

#pragma once

#include "ScenePortal.h"

struct secplane_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

class PlaneMirrorPortal : public ScenePortal
{
public:
	explicit PlaneMirrorPortal(secplane_t* plane) : m_origin(plane)
	{
	}

	PortalType Type() const override { return PortalType::PlaneMirror; }
	const void* GetSource() const override { return m_origin; }
	const char* GetName() const override;
	int GetMirrorSide() const override;
	void DrawPortalStencil(PortalDriver& driver, const PortalFrameView& outer, int pass) override;

protected:
	bool Setup(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip) override;
	void Shutdown(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp) override;

private:
	secplane_t* m_origin;
	int m_oldPlaneMirrorMode = 0;
	StencilRange m_flatRange;
	StencilRange m_edgeRange;
	bool m_geometryBuilt = false;

	void SetupCoverage(FLevelLocals* level, PortalClip& clip) const;
	void BuildGeometry(PortalDriver& driver, const PortalFrameView& outer);
};

}
