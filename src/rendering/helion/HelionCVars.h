/*
** HelionCVars.h
**
** CVars of the Helion-style renderer (plan decision D28). Declared here, defined in HelionCVars.cpp.
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

#include "c_cvars.h"

// 0 = truecolor (default), 1 = palette (indexed colormap lookups, like Helion's PaletteColorMode).
EXTERN_CVAR(Int, vid_colormode)
// Helion's VanillaRender: software sprite clipping emulation (plane/wall clip buffers).
EXTERN_CVAR(Bool, gl_vanillarender)
// Helion's EmulateInvulnerabilityColorMap: map the invulnerability tint through a palette-like gray ramp.
EXTERN_CVAR(Bool, gl_emulateinvulncolormap)

// Visibility mode (plan D18): 0 = static render blockmap, 1 = BSP, 2 = adaptive between the two.
EXTERN_CVAR(Int, gl_rendermode)
// Adaptive mode: BSP time (microseconds) below which BSP is used, hysteresis fraction around it, segment count
// above which BSP is never used, size of the median window of the BSP time samples.
EXTERN_CVAR(Int, gl_rendermode_timethreshold)
EXTERN_CVAR(Float, gl_rendermode_hysteresis)
EXTERN_CVAR(Int, gl_rendermode_segthreshold)
EXTERN_CVAR(Int, gl_rendermode_timewindow)
// Helion DownScaleVanillaRenderSampleBuffer (1 = off).
EXTERN_CVAR(Float, gl_vanillarender_downscale)
// Helion PixelGapCorrection (VertexGapClampUV).
EXTERN_CVAR(Bool, gl_pixelgapcorrection)
// Helion FuzzAmount (Renderer.GetFuzzDiv).
EXTERN_CVAR(Float, gl_fuzzamount)
// Helion AlternateInvulnerabilityOverlay: tint colour overlay instead of the inverse-colormap effect.
EXTERN_CVAR(Bool, gl_altinvulnoverlay)
EXTERN_CVAR(Color, gl_altinvulncolor)
