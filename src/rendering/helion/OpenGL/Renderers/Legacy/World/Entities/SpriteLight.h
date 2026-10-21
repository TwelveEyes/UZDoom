/*
** SpriteLight.h
**
** UZDoom-only (plan M4.1): the sprite light helpers of hw_spritelight.cpp (GetDynSpriteLight / FindLightProbe) and the
** fullbright flags of classic SetFullbrightFlags, as free functions shared by the entity, particle, model and weapon code.
**
**---------------------------------------------------------------------------
**
** Copyright 2002-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#pragma once

class AActor;
struct FLevelLocals;
struct FSection;
struct LightProbe;
struct particle_t;
class player_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

// classic Fullbright / Nightvision / StealthVision.
struct FullbrightState
{
	bool Fullbright = false;
	bool Nightvision = false;
	bool StealthVision = false;
};

// classic SetFullbrightFlags (without the colormap result, the colormap is applied by the shader uniforms).
// player = r_viewpoint.player for the main view.
FullbrightState GetFullbrightState(player_t* player);

// The nearest light probe of the level's probe grid, nullptr if the level has none.
LightProbe* FindLightProbe(FLevelLocals* level, float x, float y, float z);

// classic GetDynSpriteLight: the sum of the dynamic lights (and the light probe) at the location, out = r, g, b.
void GetDynSpriteLight(FLevelLocals* level, AActor* self, float x, float y, float z, FSection* section, int portalgroup, float* out);

// The actor / particle variant: honours RF2_NODYNAMICLIGHTING and SPF_NODYNAMICLIGHTING, out = 0 when neither is given.
void GetDynSpriteLight(FLevelLocals* level, AActor* thing, particle_t* particle, float* out);

}
