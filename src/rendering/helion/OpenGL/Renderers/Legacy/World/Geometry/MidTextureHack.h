/*
** MidTextureHack.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.MidTextureHack.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** UZDoom differences:
**  - UZDoom has no island geometry: FindContainingFloodSectors compares the bounding boxes of the sectors
**    (computed once from the vertices of the sector lines) instead of the island boxes.
**  - SectorPlane.MidTextureHack and Side.MidTextureFlood are kept in RenderStaticState.
**  - sector_t has no per plane NoRender state that can be written (PLANEF_NORENDER belongs to the game), so the
**    NoRender bit the hack sets is kept in a mask here, see PlaneNoRender (RenderPlane::From ORs it in).
*/

#pragma once

#include <unordered_set>
#include <vector>
// Must precede the namespace below: helion headers use an unqualified Geometry:: that would find World::Geometry.
#include "Common/Shared/RenderInfo.h"
#include "World/Geometry/Sectors/SectorPlaneFace.h"

struct sector_t;
struct FLevelLocals;

namespace Helion::Render::OpenGL::Texture::Legacy { class LegacyGLTextureManager; }

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

class GeometryRenderer;

class MidTextureHack
{
public:
	void Apply(FLevelLocals* world, Texture::Legacy::LegacyGLTextureManager* glTextureManager, GeometryRenderer& geometryRenderer);
	void Apply(FLevelLocals* world, const std::vector<int>& sectorIds, Texture::Legacy::LegacyGLTextureManager* glTextureManager,
		GeometryRenderer& geometryRenderer);

	// [UZDoom] The NoRender bit the hack set on a plane of the sector. pos is sector_t::floor / sector_t::ceiling.
	static bool PlaneNoRender(const sector_t* sector, int pos);
	// [UZDoom] Drops the NoRender mask and the sector bounding boxes. Call on level change.
	static void Clear();

private:
	void ApplyToSector(FLevelLocals* world, Texture::Legacy::LegacyGLTextureManager* glTextureManager, GeometryRenderer& geometryRenderer,
		sector_t* sector);
	static void SetSectorForMidTextureHack(sector_t* sector, bool clippedFloor, bool clippedCeiling);
	void FindContainingFloodSectors(FLevelLocals* world, sector_t* sector, std::vector<sector_t*>& sectors, bool floor, bool ceiling);
	void SetSectorLinesForMidTextureHack(FLevelLocals* world, GeometryRenderer& geometryRenderer, sector_t* sector,
		::Helion::World::Geometry::Sectors::SectorPlaneFace face);
	static void PrepareLevel(FLevelLocals* world);
	static bool PlaneNoRenderOrFlag(const sector_t* sector, int pos);
	void ClearApplyState();

	std::unordered_set<int> m_midTextureHackLines;
	std::unordered_set<int> m_floodLineSet;
	std::unordered_set<int> m_midTextureHackSectors;
	std::vector<sector_t*> m_containingSectors;
};

}
