/*
** MathHelper.h
**
** Port of the parts of Helion.Util.MathHelper used by the renderer.
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

#include <algorithm>
#include <numbers>

namespace Helion::Util
{

struct MathHelper
{
	static constexpr double Pi = std::numbers::pi;
	static constexpr double TwoPi = 2.0 * std::numbers::pi;
	static constexpr double HalfPi = std::numbers::pi / 2.0;

	static constexpr double ToRadians(double degrees) { return degrees * (Pi / 180.0); }
	static constexpr double ToDegrees(double radians) { return radians * (180.0 / Pi); }

	template<typename T>
	static constexpr T Clamp(T value, T min, T max) { return std::clamp(value, min, max); }
};

}
