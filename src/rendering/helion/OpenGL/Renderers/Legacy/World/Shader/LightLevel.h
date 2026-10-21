/*
** LightLevel.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Shader.LightLevel.
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

#include "zstring.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Shader
{

enum class LightLevelOptions
{
	Default = 0,
	NoDist
};

enum class VertexLightBufferOptions
{
	Default = 0,
	LightLevelAdd
};

class LightLevel
{
public:
	static FString VertexVariables(LightLevelOptions options);
	static FString VertexLightBufferVariables();
	static FString VertexLightBuffer(VertexLightBufferOptions options);
	static FString VertexDist(const FString& posVariable);
	static FString FragVariables(LightLevelOptions options);
	// UZDoom-only: the flat uzLightFrag varying (qualifier "in" / "out") and the texture warp helper of the array programs.
	static FString UzdoomLightVariables(const char* qualifier);
	static FString WarpFunction();

	// Light projection calculation is: (projection >> LIGHTSCALESHIFT) / distance
	// 160 * 65536 / 4096 / distance
	// 160 = (projection) half screen width (320).
	// 65536 = fixed point
	// 4096 = LIGHTSCALESHIFT.
	// 160 * 65536 / 4096 = 2560

	// 47 = MAXLIGHTSCALE - 1
	// startmap from R_ExecuteSetViewSize in r_main
	static FString FragFunction();
};

}
