/*
** VertexFunction.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Shader.VertexFunction.
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

class VertexFunction
{
public:
	static FString VertexDistVar3D();
	static FString SetVertexDist3D(const FString& posVariable);
	static FString VertexGapVariables();

	// Clamps the uv coordintes so that the extended ranges repeat pixels instead of reading the next row/col of pixels.
	// The uv clamp frag variables are not interpolated but are only set by the provoking vertex.
	// This means it can only clamp by the top left and bottom right values, making the top right and bottom left edges incorrect.
	static FString VertexGapSet();

	static FString VertexOptionsSet();
	static FString ColorMapAndLightLevelSet();
	static FString LightLevelAddAndMapIdSet();
};

}
