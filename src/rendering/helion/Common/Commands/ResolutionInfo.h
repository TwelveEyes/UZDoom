/*
** ResolutionInfo.h
**
** Port of Helion.Render.OpenGL.Commands.ResolutionInfo.
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

namespace Helion::Render::OpenGL::Commands
{

/// <summary>
/// Information when handling resolution.
/// </summary>
struct ResolutionInfo
{
	/// <summary>
	/// The virtual dimension that should be used when drawing.
	/// </summary>
	Geometry::Dimension VirtualDimensions;

	/// <summary>
	/// How to scale the image to the virtual resolution. This has no
	/// effect if the virtual dimension is the same scaling as the native
	/// resolution (as in 16:10 window with a 16:10 virtual dimension means
	/// this will have no effect).
	/// </summary>
	Common::Enums::ResolutionScale Scale = Common::Enums::ResolutionScale::None;

	/// <summary>
	/// The aspect ratio to use. Note that this is the aspect ratio used
	/// and not the aspect ratio of VirtualDimensions
	/// </summary>
	float AspectRatio = 0;

	ResolutionInfo() = default;

	ResolutionInfo(Geometry::Dimension virtualDimensions, Common::Enums::ResolutionScale scale, float aspectRatio) :
		VirtualDimensions(virtualDimensions),
		Scale(scale),
		AspectRatio(aspectRatio)
	{
	}
};

}
