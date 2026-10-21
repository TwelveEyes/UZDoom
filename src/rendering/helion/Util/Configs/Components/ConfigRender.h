/*
** ConfigRender.h
**
** Port of the RenderLightMode enum of Helion.Util.Configs.Components.ConfigRender.
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

namespace Helion::Util::Configs::Components
{

// Banded and Smooth are Helion's modes. Uzdoom is UZDoom specific: the light level is run through
// R_DoomLightingEquation, which follows gl_lightmode (carried by the viewpoint block's uPalLightLevels).
enum class RenderLightMode
{
	Banded,
	Smooth,
	Uzdoom
};

}
