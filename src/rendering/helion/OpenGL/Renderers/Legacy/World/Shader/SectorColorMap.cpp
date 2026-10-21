/*
** SectorColorMap.cpp
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

#include "SectorColorMap.h"
#include "OpenGL/Renderers/Legacy/World/ShaderUniforms.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Shader
{

FString SectorColorMap::VertexFragVariables()
{
	return R"GLSL(flat out vec3 sectorColorMapIndexFrag;
flat out vec4 sectorFogColorFrag;)GLSL";
}

FString SectorColorMap::VertexUniformVariables()
{
	return R"GLSL(uniform samplerBuffer sectorColormapTexture;
uniform samplerBuffer sectorFogTexture;)GLSL";
}

FString SectorColorMap::VertexFunctionWorld()
{
	return VertexFunction("colorMapIndexFrag", "sectorColorMapIndexFrag", "sectorFogColorFrag");
}

FString SectorColorMap::VertexFunction(const FString& indexVar, const FString& colorVar, const FString& fogVar)
{
	FString text;
	text << "\n            if (useSectorColor == 1)\n"
		<< "                " << colorVar << " = texelFetch(sectorColormapTexture, int(" << indexVar << ")).rgb;\n"
		<< "            else\n"
		<< "                " << colorVar << " = vec3(1);\n\n"
		<< "            if (useSectorFog >= 1)\n";
	text.AppendFormat("                %s = texelFetch(sectorFogTexture, int(mix(%s, 0, float(useSectorFog == %d)))).rgba;\n",
		fogVar.GetChars(), indexVar.GetChars(), ShaderUniforms::ViewBlendFog);
	text << "            else\n"
		<< "                " << fogVar << " = vec4(0);\n            ";
	return text;
}

FString SectorColorMap::FragVariables()
{
	return R"GLSL(flat in vec3 sectorColorMapIndexFrag;
flat in vec4 sectorFogColorFrag;
uniform int fogBarrier;)GLSL";
}

FString SectorColorMap::FragFunction()
{
	return "";
}

}
