/*
** VirtualResolutionInfo.h
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
** HudBox / Align are part of Helion's HUD layout and are not ported: VirtualTranslate (which needs both) is
** omitted, VirtualToParent works on a Box2I (top left / size convention of HudBox: Min = top left, Max =
** Min + size is expressed as Min/Max here).
*/

#pragma once

#include <optional>
#include "Common/Enums/ResolutionScale.h"
#include "Util/Geometry.h"

namespace Helion::Render::Common
{

/// <summary>
/// Information for a virtual resolution when rendering.
/// </summary>
struct VirtualResolutionInfo
{
	Geometry::Dimension Dimension;
	Enums::ResolutionScale ResolutionScale;
	Geometry::Vec2F Scale;
	Geometry::Vec2F Gutter;

	VirtualResolutionInfo(Geometry::Dimension dimension, Enums::ResolutionScale resolutionScale, Geometry::Dimension parentDimension,
		std::optional<float> aspectRatioOverride = std::nullopt);

	/// <summary>
	/// Takes a box in the virtual space of this resolution, and transforms
	/// it into the parent space.
	/// </summary>
	Geometry::Box2I VirtualToParent(const Geometry::Box2I& virtualBox) const;
};

}
