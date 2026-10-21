/*
** FragFunction.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Shader.FragFunction.
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

enum class FragColorFunctionOptions
{
	None = 0,
	AddAlpha = 1,
	Alpha = 2,
	Fuzz = 4,
	Colormap = 8,
	VertexGapClampUV = 16,
	Brightmaps = 32,

	// UZDoom-only: the color comes from the mod shader contract (Material / SetupMaterial / ProcessLight /
	// ProcessMaterialLight, see UzdoomContract.h) instead of boundTexture and the Helion brightmap and
	// light level multiplication. Only valid for truecolor programs.
	UzdoomMaterial = 64,

	// UZDoom-only: truecolor light level from R_DoomLightingEquation when the lightMode uniform is RenderLightMode::Uzdoom.
	// The program needs UzdoomContract::LightModePrelude() and the worldPosFrag varying.
	UzdoomLightMode = 128,

	// UZDoom-only (plan D25): with UzdoomLightMode, the clustered dynamic lights of the world (HelionDynamicLight of
	// shaders/glsl/helion_dynlight.fp). The program needs UzdoomContract::LightModePrelude(true).
	DynamicLights = 256,
};

constexpr FragColorFunctionOptions operator|(FragColorFunctionOptions a, FragColorFunctionOptions b)
{
	return (FragColorFunctionOptions)((int)a | (int)b);
}

constexpr bool HasOption(FragColorFunctionOptions options, FragColorFunctionOptions flag)
{
	return ((int)options & (int)flag) != 0;
}

enum class ColorMapFetchContext { Default, Hud, Entity };

enum class OitOptions
{
	None,
	OitTransparentPass,
	OitCompositePass,
	OitFuzzRefractionPass
};

enum class FuzzRefractionOptions
{
	Hud,
	World
};

class FragFunction
{
public:
	static FString VertexDistVar3D();

	static FString OitFragVariables(OitOptions options);
	static FString FuzzFunction();
	static FString AlphaFlag(bool lightLevel);
	static FString ColorMapFetch(bool lightLevel, ColorMapFetchContext ctx);
	static FString FragColorFunction(FragColorFunctionOptions options, ColorMapFetchContext ctx = ColorMapFetchContext::Default,
		OitOptions oitOptions = OitOptions::None, const FString& postProcess = FString());
	static FString FuzzRefractionFunction(FuzzRefractionOptions options);
	static FString GammaCorrection();
	static FString InvulnerabilityFragColor();
	static FString InvulnerabilityFragColorInner();
	static FString VertexGapVariables();

	// UZDoom-only. The truecolor light stage of the array programs (FragColorFunctionOptions::UzdoomLightMode outside palette
	// mode): HelionGetLight of shaders/glsl/helion_lightmode.fp (gl_lightmode, fog, sector colour, desaturation), wall glow,
	// brightmaps. Needs UzdoomContract::LightModePrelude(), the worldPosFrag / dist2D varyings and uzLightFrag.
	static FString UzdoomLight(FragColorFunctionOptions options);

	// UZDoom-only (r_dithertransparency, main.fp DITHERTRANS): the post process of the world programs that discards the
	// ordered dither pattern of surfaces with the flat ditherFrag varying set. Needs worldPosFrag.
	static FString DitherTransparency();

private:
	static FString BrightMapLightColorIndexFetch(const FString& uvVar);
	static FString GetTextureMappingClamp(FragColorFunctionOptions options);
	static FString GetBrightMapBlend(FragColorFunctionOptions options);
	static FString GetClearAlpha(OitOptions oitOptions);
	static FString FuzzDist(FuzzRefractionOptions options);
	static FString Oit(OitOptions options, FragColorFunctionOptions fragColorOptions);

	// UZDoom-only: the first lines of the color function for UzdoomMaterial programs and the light stage replacing the brightmap/lightLevel multiplication.
	static FString MaterialColor(const FString& declareFragColor, const FString& textureMappingClamp);
	static FString MaterialLightStage();
};

}
