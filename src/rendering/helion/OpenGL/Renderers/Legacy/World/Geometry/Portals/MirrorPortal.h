/*
** MirrorPortal.h
**
** UZDoom-only (plan M5.2). Port of HWMirrorPortal (hw_portal.h / hw_portal.cpp): a line with the Line_Mirror special reflects the
** view. The portal view is the world seen from the viewpoint mirrored at the line.
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

class MirrorPortal : public LinePortal
{
public:
	explicit MirrorPortal(line_t* line) : LinePortal(line), m_linedef(line)
	{
	}

	PortalType Type() const override { return PortalType::Mirror; }
	const void* GetSource() const override { return m_linedef; }
	const char* GetName() const override;

protected:
	bool Setup(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip) override;
	void Shutdown(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp) override;

private:
	line_t* m_linedef;
};

}
