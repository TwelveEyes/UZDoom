/*
** HelionCVars.cpp
**
** CVars of the Helion-style renderer (plan decision D28).
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

#include "HelionCVars.h"
#include "colorspace.h"

CVAR(Int, vid_colormode, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, gl_vanillarender, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, gl_emulateinvulncolormap, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// 0 = static (render blockmap), 1 = BSP, 2 = adaptive (Helion AdaptiveRenderMode Static / Bsp / Adaptive)
CVAR(Int, gl_rendermode, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, gl_rendermode_timethreshold, 2200, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_rendermode_hysteresis, 0.05f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, gl_rendermode_segthreshold, 8000, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, gl_rendermode_timewindow, 10, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_vanillarender_downscale, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, gl_pixelgapcorrection, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, gl_fuzzamount, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, gl_altinvulnoverlay, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Color, gl_altinvulncolor, Color::str("#00ff00"), CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
