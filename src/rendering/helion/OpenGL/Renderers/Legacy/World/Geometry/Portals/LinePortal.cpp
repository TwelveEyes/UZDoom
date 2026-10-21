/*
** LinePortal.cpp
**
** UZDoom-only (plan M5.2), see LinePortal.h.
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

#include "LinePortal.h"
#include "p_maputl.h"
#include "portal.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

LinePortal::LinePortal(line_t* line)
{
	v1 = line->v1;
	v2 = line->v2;
	CalcDelta();
}

LinePortal::LinePortal(FLinePortalSpan* span)
{
	if (span->lines[0]->mType != PORTT_LINKED || span->v1 == nullptr)
	{
		// For non-linked portals we must check the actual linedef.
		line_t* lline = span->lines[0]->mDestination;
		v1 = lline->v1;
		v2 = lline->v2;
	}
	else
	{
		// For linked portals we can check the merged span.
		v1 = span->v1;
		v2 = span->v2;
	}
	CalcDelta();
}

void LinePortal::CalcDelta()
{
	delta = v2->fPos() - v1->fPos();
}

int LinePortal::ClipSeg(seg_t* seg, const DVector3& viewpos) const
{
	line_t* linedef = seg->linedef;
	if (!linedef)
	{
		return PClipInside;	// should be handled properly.
	}
	return P_ClipLineToPortal(linedef, const_cast<LinePortal*>(this), viewpos.XY()) ? PClipInFront : PClipInside;
}

int LinePortal::ClipSubsector(subsector_t* sub) const
{
	// this seg is completely behind the mirror
	for (unsigned int i = 0; i < sub->numlines; i++)
	{
		if (P_PointOnLineSidePrecise(sub->firstline[i].v1->fPos(), this) == 0)
			return PClipInside;
	}
	return PClipInFront;
}

int LinePortal::ClipPoint(const DVector2& pos) const
{
	if (P_PointOnLineSidePrecise(pos, this))
	{
		return PClipInFront;
	}
	return PClipInside;
}

}
