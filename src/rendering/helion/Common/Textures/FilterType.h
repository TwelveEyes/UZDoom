/*
** FilterType.h
**
** Port of Helion.Render.Common.Textures.FilterType.
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

namespace Helion::Render::Common::Textures
{

enum class FilterType
{
	Nearest,
	Bilinear,
	Trilinear,
	/* [Description("Nearest to bilinear")] */
	NeareastBilinear,
	/* [Description("Nearest to trilinear")] */
	NearestTrilinear
};

}
