/*
** RenderDimensions.cpp
**
** Port of Helion.Render.Common.RenderDimensions.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include "Common/RenderDimensions.h"

namespace Helion::Render::Common
{

using namespace Helion::Geometry;
using Enums::ResolutionScale;

Vec2I RenderDimensions::Translate(const Vec2I& point, const Dimension& parentViewport) const
{
	Vec2D gutter(0, 0);
	Vec2D scale(parentViewport.Width * m_inverseScale.X, parentViewport.Height * m_inverseScale.Y);

	if (ScaleType == ResolutionScale::Stretch)
		return Vec2I((int)(point.X * scale.X), (int)(point.Y * scale.Y));

	// If the scaling is not equal, then we have a gutter to worry about.
	//
	// The idea is as follows (assuming the virtual is smaller):
	//
	// Place your resolutions both at the top left corner, and grab the
	// bottom right corner of the virtual resolution and stretch it by
	// the same scaling in both directions until it touches an edge.
	// It will either touch the bottom, or the right, or both. If it
	// does not touch the bottom right, then the aspect ratio's mismatch.
	// The solution then is to take the smaller scale, and use that for
	// both axes.
	//
	// What happens is that it works fine for one axis, but not the other
	// axis. There's extra space, which we call the 'gutter'. This has to
	// be handled by finding out how wide or tall it is, and adding it if
	// needed. If we're centering, then the solution is to add half of the
	// gutter to the result.
	//
	// Therefore, our gutter formula is:
	//      gutter = fullDimension - (dimension * smallerScale)
	if (scale.X > scale.Y)
	{
		if (ScaleType == ResolutionScale::Center)
			gutter.X = (parentViewport.Width - (Width * scale.Y)) / 2;
		scale.X = scale.Y;
	}
	else if (scale.X < scale.Y)
	{
		if (ScaleType == ResolutionScale::Center)
			gutter.Y = (parentViewport.Height - (Height * scale.X)) / 2;
		scale.Y = scale.X;
	}

	Vec2D scaledPoint(point.X * scale.X, point.Y * scale.Y);
	return Vec2I((int)(scaledPoint.X + gutter.X), (int)(scaledPoint.Y + gutter.Y));
}

}
