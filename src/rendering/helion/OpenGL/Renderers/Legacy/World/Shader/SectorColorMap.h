/*
** SectorColorMap.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Shader.SectorColorMap.
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

class SectorColorMap
{
public:
	static FString VertexFragVariables();
	static FString VertexUniformVariables();
	static FString VertexFunctionWorld();
	static FString VertexFunction(const FString& indexVar, const FString& colorVar, const FString& fogVar);
	static FString FragVariables();
	static FString FragFunction();
};

}
