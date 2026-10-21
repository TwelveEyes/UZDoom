/*
** ShaderUniforms.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.ShaderUniforms.
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

#include "Util/Geometry.h"
#include "Graphics/Palettes/PaletteIndex.h"
#include "Util/Configs/Components/ConfigRender.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

struct ColorMapUniforms
{
	int GlobalIndex = 0;
	int SkyIndex = 0;
	int SectorIndex = 0;
};

struct ColorMixUniforms
{
	::Helion::Geometry::Vec3F Global = ::Helion::Geometry::Vec3F(1, 1, 1);
	::Helion::Geometry::Vec3F Sky = ::Helion::Geometry::Vec3F(1, 1, 1);
	::Helion::Geometry::Vec3F Sector = ::Helion::Geometry::Vec3F(1, 1, 1);
};

// A struct in Helion (primary constructor); the default constructor leaves the values neutral.
struct ShaderUniforms
{
	static constexpr int ViewBlendFog = 2;
	static constexpr int NoViewBlendSectorIndex = -1;
	static constexpr int ExtraLightFixedIndex = -100;

	static int GetFixedColorMapIndex(int index)
	{
		return -ExtraLightFixedIndex + index;
	}

	::Helion::Geometry::mat4 Mvp;
	::Helion::Geometry::mat4 MvpNoPitch;
	float TimeFrac = 0;
	float Mix = 1;
	bool DrawInvulnerability = false;
	int ExtraLightOrColorMapIndex = 0;
	float DistanceOffset = 0;
	ColorMixUniforms ColorMix;
	float FuzzDiv = 0;
	ColorMapUniforms ColorMapUniforms;
	Graphics::Palettes::PaletteIndex PaletteIndex = Graphics::Palettes::PaletteIndex::Normal;
	::Helion::Util::Configs::Components::RenderLightMode LightMode = ::Helion::Util::Configs::Components::RenderLightMode::Banded;
	float GammaCorrection = 1;
	int MaxDistance = 0;
	bool UseBrightmaps = false;
	bool SectorColor = false;
	bool SectorFog = false;
	int SectorFogIndex = 0;
	int ViewBlendFogSectorIndex = NoViewBlendSectorIndex;
	float DownScaleAmount = 1;

	ShaderUniforms() = default;

	ShaderUniforms(
		const ::Helion::Geometry::mat4& mvp,
		const ::Helion::Geometry::mat4& mvpNoPitch,
		float timeFrac,
		bool drawInvulnerability,
		float mix,
		int extraLightOrColorMapIndex,
		float distanceOffset,
		const ColorMixUniforms& colorMix,
		float fuzzDiv,
		const struct ColorMapUniforms& colorMapUniforms,
		Graphics::Palettes::PaletteIndex paletteIndex,
		::Helion::Util::Configs::Components::RenderLightMode lightMode,
		float gammaCorrection,
		int maxDistance,
		bool useBrightmaps,
		bool sectorColor,
		bool sectorFog,
		int viewBlendSectorFogIndex,
		float downScaleAmount)
		: Mvp(mvp),
		MvpNoPitch(mvpNoPitch),
		TimeFrac(timeFrac),
		Mix(mix),
		DrawInvulnerability(drawInvulnerability),
		ExtraLightOrColorMapIndex(extraLightOrColorMapIndex),
		DistanceOffset(distanceOffset),
		ColorMix(colorMix),
		FuzzDiv(fuzzDiv),
		ColorMapUniforms(colorMapUniforms),
		PaletteIndex(paletteIndex),
		LightMode(lightMode),
		GammaCorrection(gammaCorrection),
		MaxDistance(maxDistance),
		UseBrightmaps(useBrightmaps),
		SectorColor(sectorColor),
		SectorFog(sectorFog),
		SectorFogIndex(sectorFog ? (viewBlendSectorFogIndex >= 0 ? ViewBlendFog : 1) : 0),
		ViewBlendFogSectorIndex(viewBlendSectorFogIndex),
		DownScaleAmount(downScaleAmount)
	{
	}
};

}
