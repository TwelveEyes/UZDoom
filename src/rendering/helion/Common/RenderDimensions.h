/*
** RenderDimensions.h
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

#pragma once

#include "Common/Enums/ResolutionScale.h"
#include "Util/Geometry.h"

namespace Helion::Render::Common
{

/// <summary>
/// Cached information that lets a HUD renderer apply transformations for
/// an image based on offsets for a virtual dimension.
/// </summary>
struct RenderDimensions
{
	int Width;
	int Height;
	Enums::ResolutionScale ScaleType;

	RenderDimensions(const Geometry::Dimension& dimension, Enums::ResolutionScale scale = Enums::ResolutionScale::None) :
		RenderDimensions(dimension.Width, dimension.Height, scale)
	{
	}

	RenderDimensions(int width, int height, Enums::ResolutionScale scale = Enums::ResolutionScale::None) :
		Width(width),
		Height(height),
		ScaleType(scale),
		m_inverseScale(1.0 / width, 1.0 / height)
	{
	}

	operator Geometry::Dimension() const { return Geometry::Dimension(Width, Height); }

	/// <summary>
	/// Translates Doom-specific HUD offsets.
	/// </summary>
	/// <param name="offset">The offset of the image.</param>
	/// <returns>The translated point.</returns>
	static Geometry::Vec2I TranslateDoomOffset(const Geometry::Vec2I& offset) { return Geometry::Vec2I(-offset.X, -offset.Y); }

	Geometry::Vec2I Translate(const Geometry::Vec2I& point, const Geometry::Dimension& parentViewport) const;

private:
	Geometry::Vec2D m_inverseScale;
};

}
