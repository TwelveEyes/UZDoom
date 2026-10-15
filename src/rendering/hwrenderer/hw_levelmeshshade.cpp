/*
** hw_levelmeshshade.cpp
**
** Levelmesh shading resolve: the per-view inputs of resolve.comp
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The classic light chain stays authoritative. Per surface, the GPU resolve
** (wadsrc/static/shaders/levelmesh/resolve.comp) ports:
**
**  - side_t::GetLightLevel and CalcRelLight (p_sectors.cpp, hw_walls.cpp),
**    and the light level / colormap selection of HWWall::Process, PutWall,
**    HWFlat::Process / PutFlat / ProcessSector;
**  - SetColor (hw_setcolor.cpp) with CalcLightLevel / CalcLightColor
**    (hw_lighting.cpp);
**  - SetFog and SetShaderLight (hw_setcolor.cpp) with GetFogDensity
**    (hw_lighting.cpp);
**  - the push constants FRenderState / VkRenderState derive from them.
**
** This file supplies what the port must not recompute: the cvars and level
** fields it reads, RescaleLightLevel and the float rounding of the dark
** light modes as tables, and the fog table.
*/

// HEADER FILES ------------------------------------------------------------

#include "hw_levelmeshbake.h"
#include "g_levellocals.h"
#include "hwrenderer/scene/hw_drawinfo.h"
#include "hwrenderer/scene/hw_lighting.h"
#include "hwrenderer/scene/hw_portal.h"
#include "hw_cvars.h"
#include "c_cvars.h"

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

EXTERN_CVAR(Int, r_fakecontrast)
EXTERN_CVAR(Int, r_distance_cull_type)
EXTERN_CVAR(Float, r_visibility)

// resolve.comp hardcodes the light modes it branches on.
static_assert((int)ELightMode::LinearStandard == 0 && (int)ELightMode::DoomBright == 1 && (int)ELightMode::Doom == 2 &&
	(int)ELightMode::DoomDark == 3 && (int)ELightMode::DoomLegacy == 4 && (int)ELightMode::Build == 5 &&
	(int)ELightMode::ZDoomSoftware == 8 && (int)ELightMode::DoomSoftware == 16, "resolve.comp light mode constants");

// CODE --------------------------------------------------------------------

//==========================================================================
//
// LevelMesh_FillShading
//
//==========================================================================

void LevelMesh_FillShading(HWDrawInfo *di, LevelMeshShading &out)
{
	FLevelLocals *Level = di->Level;
	out = {};
	out.lightMode = (int)di->lightmode;
	uint32_t f = 0;
	if (di->isFullbrightScene()) f |= LMSH_FULLBRIGHT;
	if (Level->flags & LEVEL_HASFADETABLE) f |= LMSH_FADETABLE;
	if (Level->flags3 & LEVEL3_FORCEFAKECONTRAST) f |= LMSH_FORCEFAKECONTRAST;
	if (Level->flags2 & LEVEL2_SMOOTHLIGHTING) f |= LMSH_SMOOTHLIGHTING;
	if (Level->flags3 & LEVEL3_NOLIGHTFADE) f |= LMSH_NOLIGHTFADE;
	if (Level->brightfog) f |= LMSH_BRIGHTFOG;
	if (portalState.inskybox) f |= LMSH_INSKYBOX;
	if (Level->outsidefogdensity != 0 && APART(Level->info->outsidefog) != 0xff) f |= LMSH_OUTSIDEFOG;
	out.flags = f;
	out.fakeContrast = r_fakecontrast;
	out.fogMode = gl_fogmode;
	out.distanceCullType = r_distance_cull_type;
	out.extraLight = getExtraLight();
	// SetFog, r_distance_cull_type > 1
	out.cullFogDensity = r_distance_cull_type > 1 ? clamp<int>((int)(19200.0 / Level->culldist), 1, 255) : 0;
	out.cullColor = Level->cullcolor.d;
	out.levelFogDensity = Level->fogdensity;
	out.outsideFogDensity = Level->outsidefogdensity;
	out.outsideFog = Level->info->outsidefog;
	// SetShaderLight
	out.maxLightDist = (256.f) * (r_visibility / 8.0f);
	// FRenderState::SetFog
	const float LOG2E = 1.442692f;
	out.fogDensityScale = -LOG2E / 64000.f;
	for (int i = 0; i < 256; i++)
	{
		out.rescale[i] = RescaleLightLevel(i);
		out.darkLight[i] = CalcDarkLightLevel(i);
		out.softLight[i] = i / 255.f;
	}
	for (int t = 0; t < 2; t++)
		memcpy(out.distFog[t], GetDistFogTable(t), sizeof(out.distFog[t]));
}
