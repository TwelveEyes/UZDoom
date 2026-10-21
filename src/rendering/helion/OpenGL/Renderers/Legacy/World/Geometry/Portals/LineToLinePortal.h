/*
** LineToLinePortal.h
**
** UZDoom-only (plan M5.2). Port of HWLineToLinePortal (hw_portal.h / hw_portal.cpp): the view through a line portal (Line_SetPortal,
** linked and teleporting portals) is the world seen from the position and angle translated to the line the portal connects to.
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

#include "LinePortal.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

class LineToLinePortal : public LinePortal
{
public:
	explicit LineToLinePortal(FLinePortalSpan* span) : LinePortal(span), m_glport(span)
	{
	}

	PortalType Type() const override { return PortalType::LineToLine; }
	const void* GetSource() const override { return m_glport; }
	const char* GetName() const override;
	const linebase_t* ClipLine() const override { return this; }
	// Adds the actors on the destination side of the portal to the world pass of the portal view.
	void RenderAttached(PortalDriver& driver, const PortalFrameView& view) override;

	FLinePortalSpan* Span() const { return m_glport; }

protected:
	bool Setup(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip) override;

private:
	FLinePortalSpan* m_glport;
};

}
