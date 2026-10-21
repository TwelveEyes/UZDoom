/*
** WorldStatic.h
**
** Port of Helion.World.WorldStatic (render-visible subset).
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

#include "tarray.h"
#include "vectors.h"

struct FLevelLocals;
struct vertex_t;

namespace Helion::World
{

class WorldStatic
{
public:
	// Monotonic stamp used by the renderer to visit each sector/side/entity once per frame.
	inline static int CheckCounter = 1;

	// Helion WorldStatic.Sector3D: the level has 3D floors (set by LegacyWorldRenderer::UpdateToNewWorld).
	inline static bool Sector3D = false;

	// Geometry tuning used by WorldTriangulator / wall building.
	inline static double LineVertexGap = 0;
	inline static double LineVertexGapTopZ = 0;
	inline static double LineVertexGapBottomZ = 0;
	inline static float LineVertexOffset = 0;
	inline static float CoverWallOffset = 0;

	// UZDoom: the vertex positions flats are triangulated from (WorldTriangulator), captured by CaptureFlatVertexPositions
	// before the polyobjects are spawned. Like the classic flat vertex buffer (CreateVBO runs before PO_Init), the segs of
	// a polyobject's start area keep the positions of the polyobject lines before they were moved to their spawn spot;
	// the current vertex_t positions would stretch those subsectors across the map.
	static void CaptureFlatVertexPositions(FLevelLocals* level);
	static DVector2 FlatVertexPosition(const vertex_t* vertex);

private:
	inline static TArray<DVector2> s_flatVertexPositions;
};

}
