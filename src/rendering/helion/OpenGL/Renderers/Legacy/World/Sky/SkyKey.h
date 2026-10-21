/*
** SkyKey.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.SkyKey.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion keys a sky by (texture, options, offset). UZDoom skies have more state (HWSkyInfo): a second
** (foreground) layer for double skies, which of the two level scroll speeds drives the layer, the fade colour of
** the sector (sky fog) and skyboxes. All of it is part of SkyInfo, and a SkyKey is a SkyInfo.
**
*/

#pragma once

#include <cstdint>
#include <functional>
#include "Util/Geometry.h"
#include "Resources/Definitions/SkyOptions.h"

class FSkyBox;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky
{

/// <summary>
/// Everything that makes one sky different from another.
/// </summary>
struct SkyInfo
{
	static constexpr int NoTexture = -1;
	// Which scroll speed (Level->skyspeed1 / skyspeed2 -> hw_sky1pos / hw_sky2pos) drives a layer.
	static constexpr int NoScroll = -1;

	// FTextureID::GetIndex() of the texture of the sky (Helion: textureHandle). NoTexture = the default sky, which
	// is the level's first sky texture.
	int Texture = NoTexture;
	// Double sky (LEVEL_DOUBLESKY): the layer drawn over Texture (UZDoom HWSkyInfo::texture[1]); NoTexture if none.
	// This is Helion's SkyTransform.Foreground.
	int ForegroundTexture = NoTexture;
	Resources::Definitions::SkyOptions Options = Resources::Definitions::SkyOptions::Flip;
	// Sky transfer lines (MBF / ZDoom static init sky): x in degrees (side x offset * 360 / 65536), y in texture
	// pixels (HWSkyInfo x_offset[0], y_offset). Zero otherwise.
	::Helion::Geometry::Vec2F Offset = ::Helion::Geometry::Vec2F(0, 0);
	// 0 = Level->hw_sky1pos, 1 = Level->hw_sky2pos, NoScroll = static (sky transfer lines).
	int ScrollLayer = 0;
	// sector->Colormap.FadeColor, alpha ignored. The sky fog (Level->skyfog) tints the sky with it; 0 = none.
	uint32_t FadeColor = 0;
	// Sky textures that are GLDEFS/MAPINFO skyboxes (FSkyBox). Texture is the index of the box texture.
	bool IsSkyBox = false;
};

/// <summary>
/// Key of the sky components of LegacySkyRenderer.
/// </summary>
struct SkyKey
{
	SkyInfo Info;

	SkyKey() = default;
	explicit SkyKey(const SkyInfo& info) : Info(info) { }

	size_t GetHashCode() const
	{
		size_t hash = std::hash<int>()(Info.Texture);
		auto combine = [&hash](size_t value) { hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2); };
		combine(std::hash<int>()(Info.ForegroundTexture));
		combine(std::hash<int>()((int)Info.Options));
		combine(std::hash<float>()(Info.Offset.X));
		combine(std::hash<float>()(Info.Offset.Y));
		combine(std::hash<int>()(Info.ScrollLayer));
		combine(std::hash<uint32_t>()(Info.FadeColor));
		combine(std::hash<bool>()(Info.IsSkyBox));
		return hash;
	}

	bool operator==(const SkyKey& other) const
	{
		return Info.Texture == other.Info.Texture && Info.ForegroundTexture == other.Info.ForegroundTexture && Info.Options == other.Info.Options &&
			Info.Offset.X == other.Info.Offset.X && Info.Offset.Y == other.Info.Offset.Y && Info.ScrollLayer == other.Info.ScrollLayer &&
			Info.FadeColor == other.Info.FadeColor && Info.IsSkyBox == other.Info.IsSkyBox;
	}
};

struct SkyKeyHash
{
	size_t operator()(const SkyKey& key) const { return key.GetHashCode(); }
};

}
