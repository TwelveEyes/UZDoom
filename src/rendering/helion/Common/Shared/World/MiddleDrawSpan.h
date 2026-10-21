/*
** MiddleDrawSpan.h
**
** Port of Helion.Render.OpenGL.Shared.World.MiddleDrawSpan.
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

namespace Helion::Render::OpenGL::Shared::World
{

/// <summary>
/// Contains the drawing information for what is visible vertically in some
/// two sided middle opening so we know how to draw a middle texture.
/// </summary>
struct MiddleDrawSpan
{
	double TopZ = 0;
	double BottomZ = 0;
	double VisibleTopZ = 0;
	double VisibleBottomZ = 0;

	double PrevTopZ = 0;
	double PrevBottomZ = 0;
	double PrevVisibleTopZ = 0;
	double PrevVisibleBottomZ = 0;

	MiddleDrawSpan() = default;

	MiddleDrawSpan(double bottomZ, double topZ, double visibleBottomZ, double visibleTopZ,
		double prevBottomZ, double prevTopZ, double prevVisibleBottomZ, double prevVisibleTopZ) :
		TopZ(topZ),
		BottomZ(bottomZ),
		VisibleTopZ(visibleTopZ),
		VisibleBottomZ(visibleBottomZ),
		PrevTopZ(prevTopZ),
		PrevBottomZ(prevBottomZ),
		PrevVisibleTopZ(prevVisibleTopZ),
		PrevVisibleBottomZ(prevVisibleBottomZ)
	{
	}

	/// <summary>
	/// Checks if the span is not visible and we can't render anything.
	/// </summary>
	/// <returns>True if we cannot see it and should exit early instead of
	/// trying to render it, false if we can see something and it needs to
	/// be rendered.</returns>
	bool NotVisible() const { return VisibleTopZ <= VisibleBottomZ && PrevVisibleTopZ <= PrevVisibleBottomZ; }
};

}
