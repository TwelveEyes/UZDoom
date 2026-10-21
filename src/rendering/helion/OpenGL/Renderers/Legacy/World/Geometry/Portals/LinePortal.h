/*
** LinePortal.h
**
** UZDoom-only (plan M5.2). Port of HWLinePortal (hw_portal.h / hw_portal.cpp): the code that mirror portals and line to line
** portals share, the clipping of segs, subsectors and points against the portal line.
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
** The portal is a linebase_t (the line the view is clipped against), like HWLinePortal.
**
*/

#pragma once

#include "ScenePortal.h"
#include "r_defs.h"

struct FLinePortalSpan;
struct line_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

class LinePortal : public ScenePortal, public linebase_t
{
public:
	// Mirror portals always consist of single linedefs.
	explicit LinePortal(line_t* line);
	explicit LinePortal(FLinePortalSpan* span);

	// The clip of a seg against the portal line (P_ClipLineToPortal): PClipInFront = behind the portal line, not seen.
	int ClipSeg(seg_t* seg, const DVector3& viewpos) const override;
	// This subsector is completely behind the portal line.
	int ClipSubsector(subsector_t* sub) const override;
	int ClipPoint(const DVector2& pos) const override;
	bool NeedCap() const override { return false; }

protected:
	void CalcDelta();
};

}
