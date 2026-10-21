/*
** ModelLightList.h
**
** UZDoom-only (plan M6.2): the dynamic lights of one model or HUD model (hw_spritelight.cpp hw_GetDynModelLight).
**
** The clustered lights of the world (ViewLights) are one list per view; a model needs the per actor tests of
** FDynamicLight::ShouldLightActor (LF_DONTLIGHTSELF: not the light's own actor, LF_DONTLIGHTOTHERS: only the light's
** own actor), which a list shared by every model of the view cannot make. So models keep the classic per draw list:
** the lights of the sections the actor touches, always attenuated, uploaded to the frame's FLightBuffer
** (screen->mLights, which must be mapped) in the AddLightToList record layout that lights[uLightIndex] reads.
** ModelProgram::DrawLights binds the buffer and passes the index to the shader.
**
**---------------------------------------------------------------------------
**
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

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

class ModelLightList
{
public:
	// The FLightBuffer index of the actor's light list, -1 when no light touches it (or the buffer is full).
	static int Upload(AActor* self);
};

}
