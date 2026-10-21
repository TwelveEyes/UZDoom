/*
** EntityCVars.cpp
**
** UZDoom-only (plan M4, D16): the sprite, particle, model and weapon CVARs the entity renderers read. They were defined
** in hw_sprites.cpp, hw_spritelight.cpp / hw_dynlightdata.cpp, hw_drawinfo.cpp, hw_models.cpp and hw_lighting.cpp of the
** deleted classic pipeline. Names, types, defaults and flags are unchanged.
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

#include "c_cvars.h"

// Sprites (hw_sprites.cpp)
CVAR(Bool, gl_usecolorblending, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, gl_sprite_blend, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR(Int, gl_spriteclip, -1, CVAR_ARCHIVE)
CVAR(Bool, r_debug_nolimitanamorphoses, false, 0)
CVAR(Float, r_spriteclipanamorphicminbias, 0.6, CVAR_ARCHIVE)
CVAR(Float, gl_sclipthreshold, 10.0, CVAR_ARCHIVE)
CVAR(Float, gl_sclipfactor, 1.8f, CVAR_ARCHIVE)
CVAR(Int, gl_particles_style, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) // 0 = square, 1 = round, 2 = smooth
CVAR(Int, gl_billboard_mode, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, gl_billboard_faces_camera, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, hw_force_cambbpref, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, gl_billboard_particles, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CUSTOM_CVAR(Int, gl_fuzztype, 8, CVAR_ARCHIVE)
{
	if (self < 0 || self > 8) self = 0;
}

// Sprite and particle light (hw_dynlightdata.cpp). These shouldn't be called 'gl...' anymore...
CVAR (Bool, gl_light_sprites, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR (Bool, gl_light_particles, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);

// hw_drawinfo.cpp
CVAR(Int, gl_enhanced_nv_stealth, 3, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// A/B debug override: render every view as a plain fullbright scene (uniform white light, no fog, no dynamic lights, no
// player special colormap). No effect when off.
CVAR(Bool, r_ab_fullbright, false, CVAR_NOSAVE)
CVAR(Float, gl_mask_sprite_threshold, 0.5f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, gl_coronas, true, CVAR_ARCHIVE);

// Models (hw_models.cpp)
CVAR(Bool, gl_light_models, true, CVAR_ARCHIVE)

// Weapon and night vision (hw_lighting.cpp)
CVAR(Int, gl_weaponlight, 8, CVAR_ARCHIVE);
CVAR(Bool, gl_enhanced_nightvision, false, CVAR_ARCHIVE|CVAR_NOINITCALL)
