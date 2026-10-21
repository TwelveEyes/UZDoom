/*
** WorldTriangulator.h
**
** Port of Helion.Render.OpenGL.Shared.World.WorldTriangulator.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Type mapping: Side -> side_t*, Line -> line_t*, Subsector -> subsector_t*,
** SectorPlane -> Helion::World::Geometry::Sectors::SectorPlane (by-value view
** of a sector_t plane), the CompactBspTree seg list -> subsector_t::firstline.
**
** UZDoom additions (plan D29): flats are triangulated per subsector and grouped
** per FSection, reusing the triangulation of hw_vertexbuilder.cpp
** (CreateVerticesForSubsector): convex subsectors are fans, SSECF_HOLE
** subsectors with more than three lines go through earcut. Slopes get their
** vertex Z from secplane_t::ZatPoint.
*/

#pragma once

#include <cfloat>
#include <cstdint>
#include "Common/Shared/World/MidTexOpening.h"
#include "Common/Shared/World/MiddleDrawSpan.h"
#include "Common/Shared/World/TriangulatedWorldVertex.h"
#include "Common/Shared/World/WallUV.h"
#include "Common/Shared/World/WallVertices.h"
#include "Util/Geometry.h"
#include "World/Geometry/Lines/MidTexSpan.h"
#include "World/Geometry/Sectors/SectorPlane.h"
#include "World/Geometry/Sectors/SectorPlaneFace.h"
#include "tarray.h"

struct line_t;
struct side_t;
struct subsector_t;
struct FSection;

namespace Helion::Render::OpenGL::Shared::World
{

class WorldTriangulator
{
public:
	static constexpr double NoOverride = DBL_MAX;

	static void HandleOneSided(side_t* side, side_t* offsetSide,
		const ::Helion::World::Geometry::Sectors::SectorPlane& floor, const ::Helion::World::Geometry::Sectors::SectorPlane& ceiling,
		const Geometry::Vec2F& textureUVInverse, WallVertices& wall,
		double overrideFloor = NoOverride, double overrideCeiling = NoOverride, bool isFront = true, bool calculateUV = true);

	static void HandleTwoSidedLower(side_t* facingSide,
		const ::Helion::World::Geometry::Sectors::SectorPlane& topFlat, const ::Helion::World::Geometry::Sectors::SectorPlane& bottomFlat,
		const Geometry::Vec2F& textureUVInverse, bool isFrontSide, WallVertices& wall, bool calculateUV = true);

	static void HandleTwoSidedMiddle(side_t* facingSide,
		const Geometry::Dimension& textureDimension, const Geometry::Vec2F& textureUVInverse,
		const Common::Shared::World::MidTexOpening& opening, const Common::Shared::World::MidTexOpening& prevOpening,
		bool isFrontSide, WallVertices& wall, bool& nothingVisible, double offset = 0, double prevOffset = 0,
		::Helion::World::Geometry::Sectors::SectorPlanes clipPlanes = ::Helion::World::Geometry::Sectors::SectorPlanes::Floor | ::Helion::World::Geometry::Sectors::SectorPlanes::Ceiling,
		bool vertexGap = true, const ::Helion::World::Geometry::Lines::MidTexSpan* restrictSpan = nullptr);

	static void HandleTwoSidedUpper(side_t* facingSide,
		const ::Helion::World::Geometry::Sectors::SectorPlane& topPlane, const ::Helion::World::Geometry::Sectors::SectorPlane& bottomPlane,
		const Geometry::Vec2F& textureUVInverse, bool isFrontSide, WallVertices& wall,
		double overrideTopZ = NoOverride, bool calculateUV = true);

	// Replaces the contents of verticesToPopulate with the polygon of one subsector
	// (subsector.numlines vertices), in seg order for ceilings and reversed for floors like Helion.
	// textureVector is the flat texture size in texels.
	static void HandleSubsector(subsector_t* subsector, const ::Helion::World::Geometry::Sectors::SectorPlane& sectorPlane,
		bool floor, const Geometry::Vec2F& textureVector,
		TArray<TriangulatedWorldVertex>& verticesToPopulate, double overrideZ = INT32_MAX);

	// UZDoom: triangle indices for the polygon HandleSubsector produced for the same subsector and
	// 'floor' flag. Indices are offset by firstVertex and appended to 'indices'. Subsectors with
	// less than three lines produce nothing.
	static void HandleSubsectorIndices(subsector_t* subsector, bool floor, uint32_t firstVertex, TArray<uint32_t>& indices);

	// UZDoom: vertices and triangle indices for every subsector of the section, grouped per FSection
	// (plan D29). Replaces the contents of both arrays. skipFlood leaves out the subsectors flagged
	// RenderStaticState::SubsectorFlood (Helion's `subsector.Flood` check of RenderFlat).
	static void HandleSection(FSection* section, const ::Helion::World::Geometry::Sectors::SectorPlane& sectorPlane,
		bool floor, const Geometry::Vec2F& textureVector,
		TArray<TriangulatedWorldVertex>& verticesToPopulate, TArray<uint32_t>& indices, double overrideZ = INT32_MAX, bool skipFlood = false);

	// UZDoom: whether HandleSection emits the subsector (3+ lines, not skipped as flooded).
	static bool IncludeSubsector(const subsector_t* subsector, bool skipFlood);

	static WallUV CalculateOneSidedWallUV(line_t* line, side_t* side, side_t* offsetSide, double length,
		const Geometry::Vec2F& textureUVInverse, double spanZ, bool previous);

	static WallUV CalculateTwoSidedLowerWallUV(line_t* line, side_t* side, double length,
		const Geometry::Vec2F& textureUVInverse, double topZ, double bottomZ, bool previous);

	static WallUV CalculateTwoSidedUpperWallUV(line_t* line, side_t* side, double length,
		const Geometry::Vec2F& textureUVInverse, double spanZ, bool previous);

private:
	static MiddleDrawSpan CalculateMiddleDrawSpan(line_t* line, side_t* facingSide,
		const Common::Shared::World::MidTexOpening& opening, const Common::Shared::World::MidTexOpening& prevOpening,
		const Geometry::Dimension& textureDimension, double offset, double prevOffset,
		::Helion::World::Geometry::Sectors::SectorPlanes clipPlanes, bool vertexGap,
		const ::Helion::World::Geometry::Lines::MidTexSpan* restrictSpan);

	static WallUV CalculateTwoSidedMiddleWallUV(side_t* side, double length, double topZ, double bottomZ,
		double visibleTopZ, double visibleBottomZ, const Geometry::Vec2F& textureUVInverse, bool previous);

	static void GetLeftRightVertices(bool isFront, line_t* line, Geometry::Vec2F& left, Geometry::Vec2F& right);
};

}
