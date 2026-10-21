/*
** FloodFillInput.h
**
** UZDoom-only. The inputs of the flood fill: which sides flood their upper / lower wall space with the flat of the
** sector (Helion Side.FloodTextures, Sector.Flood, Side.MidTextureFlood). Helion computes them in
** StaticDataApplier.DetermineStaticData / CheckFloodFill / SetFloodFillSide at world load and on movement
** (StaticCacheGeometryRenderer.CheckForFloodFill); the UZDoom render hacks of hw_renderhacks.cpp (missing texture
** floods of lines whose upper / lower texture is missing, fake bridges) are the same idea and arrive through the same
** setters: the result of a hack is a flag on the side / sector, drawn by the FloodFillRenderer like any other flood.
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

#include "World/Geometry/Sectors/RenderSector.h"
#include "World/Static/SideTexture.h"

struct side_t;
struct line_t;
struct sector_t;
struct FLevelLocals;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals::FloodFill
{

class FloodFillInput
{
public:
	using RenderSector = ::Helion::World::Geometry::Sectors::RenderSector;
	using SideTexture = ::Helion::World::Static::SideTexture;

	// Helion StaticDataApplier.DetermineStaticData (flood part): CheckFloodFill for every two sided line.
	static void DetermineFloodFill(FLevelLocals* world);

	// Helion StaticDataApplier.CheckFloodFill: both sides of the line, using the middle transfer heights view.
	static void CheckFloodFill(line_t* line);

	static bool ShouldFloodLower(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector);
	static bool ShouldFloodUpper(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector);

	// Helion StaticDataApplier.SetFloodFillSide: recomputes the Upper / Lower bits of the FloodTextures of facingSide.
	static void SetFloodFillSide(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector);

	// --- Render hack inputs (hw_renderhacks.cpp): flags set by whoever detects the hack.
	// Sector.Flood: the sector is drawn flooded (missing texture floods, fake bridges / deep water style hacks).
	static void SetSectorFlood(sector_t* sector, bool flood);
	// Side.MidTextureFlood: planes (SectorPlanes bits) a middle texture of the side floods (the MidTextureHack result).
	static void SetSideMidTextureFlood(side_t* side, ::Helion::World::Geometry::Sectors::SectorPlanes planes);
	// Side.FloodTextures: forced flood of the upper / lower wall space of the side.
	static void AddSideFloodTextures(side_t* side, SideTexture textures);
	static void ClearSideFloodTextures(side_t* side, SideTexture textures);

	// FTextureID index of a side tier, with "no texture" (-1 / 0) mapped to 0.
	static int SideTextureHandle(const side_t* side, int tier);
};

}
