/*
** LightLevel.cpp
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

#include "LightLevel.h"
#include "ShaderVars.h"
#include "ShaderText.h"
#include "OpenGL/Renderers/Legacy/World/ShaderUniforms.h"
#include "Util/Constants.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Shader
{

FString LightLevel::VertexVariables(LightLevelOptions options)
{
	FString text = "flat out float lightLevelFrag;";
	text << (((int)options & (int)LightLevelOptions::NoDist) != 0 ? "" : "out float dist2D;");
	text << "uniform mat4 mvpNoPitch;uniform float distanceOffset;";
	// UZDoom-only: the inputs of the truecolor light stage (HelionGetLight, shaders/glsl/helion_lightmode.fp).
	text << UzdoomLightVariables("out");
	return text;
}

FString LightLevel::UzdoomLightVariables(const char* qualifier)
{
	// x = light level of the surface (sector + side / plane light), y = relative light (fake contrast), z = sector slot of the
	// colour / fog buffers, w = sector desaturation. Declared in both colour modes so the stages always match.
	FString text;
	text.Format("\nflat %s vec4 uzLightFrag;\n", qualifier);
	return text;
}

FString LightLevel::VertexLightBufferVariables()
{
	FString text = "uniform usamplerBuffer sectorLightTexture;";
	// UZDoom: the level constants of the truecolor light stage (SectorLightBuffers::GetLevelTexels).
	if (!ShaderVars::PaletteColorMode)
		text << "\nuniform samplerBuffer sectorGlowTexture;";
	return text;
}

FString LightLevel::VertexLightBuffer(VertexLightBufferOptions options)
{
	bool lightLevelAdd = ((int)options & (int)VertexLightBufferOptions::LightLevelAdd) != 0;
	FString text = R"GLSL(int texBufferIndex = int(lightLevelBufferIndex);
float lightLevelBufferValue = float(texelFetch(sectorLightTexture, texBufferIndex).r);
)GLSL";

	if (ShaderVars::PaletteColorMode)
	{
		text << "lightLevelFrag = clamp(lightLevelBufferValue";
		text << (lightLevelAdd ? " + vertexLightLevelFrag + lightLevelAddValue * (1 - sectorFogColorFrag.a) " : " + vertexLightLevelFrag");
		text << ", 0.0, 256.0);\nuzLightFrag = vec4(0.0);\n";
		return text;
	}

	// UZDoom: the fake contrast is not applied in foggy sectors unless LEVEL3_FORCEFAKECONTRAST (side_t::GetLightLevel), the
	// truecolor fog alpha is a density code.
	text << "float uzFoggy = float(any(notEqual(sectorFogColorFrag.rgb, vec3(0.0)))) * (1.0 - texelFetch(sectorGlowTexture, 1).x);\n";
	text << (lightLevelAdd ? "float uzRelLight = lightLevelAddValue * (1.0 - uzFoggy);\n" : "float uzRelLight = 0.0;\n");
	text << R"GLSL(lightLevelFrag = clamp(lightLevelBufferValue + vertexLightLevelFrag + uzRelLight, 0.0, 256.0);
uzLightFrag = vec4(clamp(lightLevelBufferValue + vertexLightLevelFrag, 0.0, 255.0), uzRelLight, colorMapIndexFrag,
    texelFetch(sectorColormapTexture, int(colorMapIndexFrag)).a);
)GLSL";
	return text;
}

FString LightLevel::VertexDist(const FString& posVariable)
{
	FString text;
	// UZDoom: the gl_lightmode equations (and mod shaders through pixelpos.w) take the depth in view space (clip w) in map units,
	// like the table shaders. Helion's palette math keeps its pitchless distance.
	if (!ShaderVars::PaletteColorMode)
		text << "dist2D = (mvp * " << posVariable << ").w;";
	else
		text << "dist2D = (mvpNoPitch * " << posVariable << ").z;";
	return text;
}

FString LightLevel::FragVariables(LightLevelOptions options)
{
	FString text = "flat in float lightLevelFrag;";
	text << (((int)options & (int)LightLevelOptions::NoDist) != 0 ? "" : "in float dist2D;");
	text << "uniform float lightLevelMix;uniform int extraLight;uniform float distanceOffset;uniform samplerBuffer colormapTexture;uniform int lightMode;uniform float gammaCorrection;";
	text << UzdoomLightVariables("in");
	text << WarpFunction();
	return text;
}

// UZDoom-only: ANIMDEFS warp / warp2 (SHADER_Warp1 / SHADER_Warp2) of the array programs, math of main.fp GetTexCoord with
// func_warp1.fp / func_warp2.fp. GLLegacyTexture::Bind sets warpParams: x = 0 none / 1 warp / 2 warp2, y = the timer.
FString LightLevel::WarpFunction()
{
	return R"GLSL(
uniform vec2 warpParams;
vec2 HelionWarpTexCoord(vec2 texCoord)
{
    const float pi = 3.14159265358979323846;
    float timer = warpParams.y;
    vec2 offset;
    if (warpParams.x < 1.5)
    {
        offset.y = sin(pi * 2.0 * (texCoord.x + timer * 0.125)) * 0.1;
        offset.x = sin(pi * 2.0 * (texCoord.y + timer * 0.125)) * 0.1;
        return texCoord + offset;
    }
    offset.y = 0.5 + sin(pi * 2.0 * (texCoord.y + timer * 0.61 + 900.0/8192.0)) + sin(pi * 2.0 * (texCoord.x * 2.0 + timer * 0.36 + 300.0/8192.0));
    offset.x = 0.5 + sin(pi * 2.0 * (texCoord.y + timer * 0.49 + 700.0/8192.0)) + sin(pi * 2.0 * (texCoord.x * 2.0 + timer * 0.49 + 1200.0/8192.0));
    return texCoord + offset * 0.025;
}
)GLSL";
}

FString LightLevel::FragFunction()
{
	FString text;
	text << "\nconst int colorMaps = 32;\n"
		<< "float lightLevel = lightLevelFrag;\n"
		<< "int lightNum = int(lightLevel / 8);\n"
		<< "int startMap = (30 - lightNum) * 2;\n";
	text.AppendFormat("float lightIndex = min(2560 / abs(dist2D), %d);\n", ::Helion::Util::Constants::LightBuffer::LightLevelsIndexMax);
	text << "float lightColor = clamp((startMap - lightIndex / 2) - extraLight, 0, 31);\n"
		<< "// Directly set the index when extraLight < 0 to the absolute value\n";
	text.AppendFormat("int lightColorIndex = int(mix(lightColor, %d + extraLight, float(extraLight < -%d)));\n",
		ShaderUniforms::ExtraLightFixedIndex, ::Helion::Util::Constants::LightBuffer::LightLevelsIndexMax);

	if (!ShaderVars::PaletteColorMode)
	{
		text << R"GLSL(
float useLightIndex = lightColor;
useLightIndex = mix(lightColorIndex, lightColor, lightMode);
lightLevel = float(colorMaps - useLightIndex) / colorMaps;
lightLevel = mix(clamp(lightLevel, 0.0, 1.0), 1.0, lightLevelMix);
lightLevel = mix(lightLevel, 1, hasInvulnerability);)GLSL";
	}

	return text;
}

}
