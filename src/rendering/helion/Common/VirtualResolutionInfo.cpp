/*
** VirtualResolutionInfo.cpp
**
** Port of Helion.Render.Common.VirtualResolutionInfo.
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

#include "Common/VirtualResolutionInfo.h"

namespace Helion::Render::Common
{

static float AspectRatioOf(Geometry::Dimension dimension)
{
	return (float)dimension.Width / (float)dimension.Height;
}

VirtualResolutionInfo::VirtualResolutionInfo(Geometry::Dimension dimension, Enums::ResolutionScale resolutionScale, Geometry::Dimension parentDimension,
	std::optional<float> aspectRatioOverride) :
	Dimension(dimension),
	ResolutionScale(resolutionScale),
	Scale(0, 0),
	Gutter(0, 0)
{
	// The aspect ratio override is used to stretch the size of the parent
	// viewport along its X axis, to which we use for the rest of the
	// calculations. Otherwise if it's not set, then using the dimension's
	// aspect ratio changes nothing.
	float aspectRatio = aspectRatioOverride.value_or(AspectRatioOf(dimension));
	float viewWidth = parentDimension.Height * aspectRatio;
	float scaleWidth = viewWidth / dimension.Width;
	float scaleHeight = parentDimension.Height / (float)dimension.Height;
	Scale = Geometry::Vec2F(scaleWidth, scaleHeight);
	Gutter = Geometry::Vec2F(0, 0);

	// By default we're stretching, but if we're centering, our values
	// have to change to accomodate a gutter if the aspect ratios are
	// different.
	if (resolutionScale == Enums::ResolutionScale::Center && AspectRatioOf(parentDimension) > aspectRatio)
	{
		// We only want to do centering if we will end up with gutters
		// on the side. This can only happen if the virtual dimension
		// has a smaller aspect ratio. We have to exit out if not since
		// it will cause weird overdrawing otherwise.
		Gutter.X = (parentDimension.Width - (int)(dimension.Width * Scale.X)) / 2.0f;
	}
}

Geometry::Box2I VirtualResolutionInfo::VirtualToParent(const Geometry::Box2I& virtualBox) const
{
	Geometry::Vec2I topLeft((int)(virtualBox.Min.X * Scale.X + Gutter.X), (int)(virtualBox.Min.Y * Scale.Y + Gutter.Y));
	Geometry::Vec2I dimension((int)(virtualBox.Width() * Scale.X), (int)(virtualBox.Height() * Scale.Y));
	return Geometry::Box2I(topLeft, Geometry::Vec2I(topLeft.X + dimension.X, topLeft.Y + dimension.Y));
}

}
