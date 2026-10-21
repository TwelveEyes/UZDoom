/*
** RenderStaticState.h
**
** UZDoom-only. The renderer-side state Helion keeps on its world objects (Wall.Static, SectorPlane.Static,
** Side.Fog, Side.FloodKeys / FloorFloodKey / CeilingFloodKey, Side.FloodTextures, Side.MidTextureFlood,
** Side.Flags.UpperSky / LowerSky, Side.SkyGeometry, SectorPlane.SkyGeometry, Sector.Flood,
** SectorPlane.MidTextureHack, Sector.MoveState / CheckCount / MoveEventGameTick / MoveProcessedGameTick,
** SectorPlane.LastRenderChangeGametick / SetSectorMoveChanged).
** sector_t and side_t carry only the ESectorDynamic bits and the render signatures (r_defs.h),
** so the rest is kept in tables indexed by sector_t::Index() / side_t::Index(), owned by the static cache
** renderer (StaticCacheGeometryRenderer::UpdateTo calls Reset).
**
** Planes are indexed by sector_t::floor / sector_t::ceiling, walls by side_t::top / side_t::mid /
** side_t::bottom (Helion's Upper / Middle / Lower).
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

#include <memory>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "r_defs.h"
#include "World/Static/SideTexture.h"
#include "World/Static/StaticGeometryData.h"
#include "World/Geometry/Sectors/SectorPlaneFace.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Static/SkyGeometryManager.h"

struct sector_t;
struct side_t;
struct FLevelLocals;

namespace Helion::World::Static
{

// Helion Sector.MoveState
enum class MoveState
{
	None,
	Start,
	Complete
};

// Side.UpperFloodKeys / LowerFloodKeys. Key1 is the normal flood wall, Key2 the alternate one (0 = none).
struct FloodKeys
{
	int Key1 = 0;
	int Key2 = 0;

	bool operator==(const FloodKeys& other) const { return Key1 == other.Key1 && Key2 == other.Key2; }
};

struct RenderSideState
{
	// side_t::top / mid / bottom
	StaticGeometryData Static[3];

	// Side.Fog: the fog barrier wall. Static.GeometryData == nullptr means "not created yet".
	bool HasFog = false;
	bool FogCleared = false;
	StaticGeometryData Fog;

	// Flood fill.
	FloodKeys UpperFloodKeys;
	FloodKeys LowerFloodKeys;
	int FloorFloodKey = 0;
	int CeilingFloodKey = 0;
	SideTexture FloodTextures = SideTexture::None;
	// Helion SectorPlanes (Floor / Ceiling bits) the middle texture floods.
	Geometry::Sectors::SectorPlanes MidTextureFlood = Geometry::Sectors::SectorPlanes::None;

	// Helion Side.Flags.UpperSky / LowerSky: the side has to be recalculated on movement.
	bool UpperSky = false;
	bool LowerSky = false;

	// Helion Side.LastRenderChangeGametick
	int LastRenderChangeGametick = -1;

	std::unique_ptr<Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static::StaticSideSkyData> SkyGeometry;
};

struct RenderPlaneState
{
	StaticGeometryData Static;
	std::unique_ptr<Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static::StaticSkyGeometryData> SkyGeometry;
	// Helion SectorPlane.MidTextureHack
	bool MidTextureHack = false;
	// Helion SectorPlane.LastRenderChangeGametick / SetSectorMoveChanged
	int LastRenderChangeGametick = -1;
	// Helion SectorPlane.Dynamic of the planes that have no sector_t: the fake planes of 3D floors (Sector3D.FakeTop etc).
	// Real planes use sector_t::renderDynamic.
	uint8_t Dynamic = 0;
};

struct RenderSectorState
{
	// Helion Sector.Flood
	bool Flood = false;

	MoveState MoveState = MoveState::None;
	int CheckCount = 0;
	int MoveEventGameTick = -1;
	int MoveProcessedGameTick = -1;
};

class RenderStaticState
{
public:
	// Sizes the tables for the level (and drops everything in them).
	static void Reset(FLevelLocals* level);
	static void Clear();

	static RenderSideState& Side(const side_t* side);
	static RenderSectorState& Sector(const sector_t* sector);
	// Sector3D::SectorId (>= the sector count) is a valid id too; the table grows on demand.
	static RenderSectorState& SectorById(int id);
	// Planes are keyed by RenderPlane::Id (sector index * 2 + sector_t::floor / sector_t::ceiling for real planes,
	// assigned by Sector3D for 3D floor planes); the table grows on demand.
	static RenderPlaneState& Plane(int planeId);
	static RenderPlaneState& Plane(const sector_t* sector, int plane) { return Plane(sector->Index() * 2 + plane); }

	// The middle wall of a 3D floor side (Helion: the Wall of the fake line side). Key = (Sector3D::SectorId, side index, wall).
	static StaticGeometryData& Wall3D(int sector3DId, const side_t* side, int wallLocation);

	// Helion Gametick.
	static int GameTick();

	// Helion SectorPlane.SetSectorMoveChanged (plane = sector_t::floor / sector_t::ceiling, or a RenderPlane::Id)
	static void SetSectorMoveChanged(const sector_t* sector, int plane, int gametick) { Plane(sector, plane).LastRenderChangeGametick = gametick; }
	static void SetSectorMoveChanged(int planeId, int gametick) { Plane(planeId).LastRenderChangeGametick = gametick; }

	// Flood fill input (Helion fills these in StaticDataApplier.SetFloodFillSide and at world load, UZDoom has no
	// such pass: the callers are the flood detection of the geometry/world code and the render hacks of hw_renderhacks.cpp).
	// See Portals/FloodFill/FloodFillInput.h.
	static void SetSectorFlood(sector_t* sector, bool flood) { Sector(sector).Flood = flood; }
	static void SetSideMidTextureFlood(side_t* side, Geometry::Sectors::SectorPlanes planes) { Side(side).MidTextureFlood = planes; }
	static void SetSideFloodTextures(side_t* side, SideTexture textures) { Side(side).FloodTextures = textures; }

	// Helion Subsector.Flood (IslandGeometry.BadSubsectors): the flats of the subsector are not drawn, the flood fill of its
	// sector covers the area. UZDoom: the subsectors of an open sector render hack (subsector_t::hacked & 1).
	static bool SubsectorFlood(const subsector_t* subsector) { return s_subsectorFlood[(size_t)subsector->Index()] != 0; }
	static void SetSubsectorFlood(const subsector_t* subsector, bool flood) { s_subsectorFlood[(size_t)subsector->Index()] = flood ? 1 : 0; }

private:
	static inline FLevelLocals* s_level = nullptr;
	static inline std::vector<RenderSideState> s_sides;
	static inline std::vector<RenderSectorState> s_sectors;
	static inline std::vector<RenderPlaneState> s_planes;
	static inline std::unordered_map<uint64_t, StaticGeometryData> s_walls3D;
	static inline std::vector<uint8_t> s_subsectorFlood;
};

}
