/*
** hw_lighting.h
**
** Light level / fog management / dynamic lights
**
**---------------------------------------------------------------------------
**
** Copyright 2002-2018 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#pragma once

#include "c_cvars.h"
#include "v_palette.h"

#include "r_utility.h"

struct Colormap;

EXTERN_CVAR(Int, r_extralight)

inline int hw_ClampLight(int lightlevel)
{
	return clamp(lightlevel, 0, 255);
}

// l * (255 - r_extralight) / 255 + r_extralight, in integers: the former
// double expression rounded differently under fast math (hw_walls.cpp and
// hw_flats.cpp build with it: 196 became 195), so the result depended on the
// compiler flags. The levelmesh shading resolve tabulates this function.
template<bool doClamp = true>
inline int RescaleLightLevel(int lightlevel) // TODO/tidy: move out of hwrenderer namespace
{
	int extralight = r_extralight;
	if constexpr(doClamp)
	{
		//max is needed for negative r_extralight values
		return max(clamp(lightlevel, 0, 255) * (255 - extralight) / 255 + extralight, 0);
	}
	else
	{
		//max is needed for negative r_extralight values
		return max(max(lightlevel, 0) * (255 - extralight) / 255 + extralight, 0);
	}
}

EXTERN_CVAR(Int, gl_weaponlight);

inline	int getExtraLight()
{
	return r_viewpoint.extralight * gl_weaponlight;
}
