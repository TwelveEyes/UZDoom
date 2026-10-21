/*
** CoverWallUtil.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.CoverWallUtil.
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

#include "OpenGL/Renderers/Legacy/World/Geometry/CoverWallUtil.h"
#include "r_defs.h"
#include "RenderBlock.h"
#include "World/WorldStatic.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

using Helion::World::Geometry::Walls::WallLocation;

namespace
{

struct Heights
{
	float AddTop;
	float SubBottom;
};

constexpr int ProjectHeight = 8192;

Heights GetProjectHeights(side_t* side, WallLocation location)
{
	if (location == WallLocation::Middle3D)
		return { 0, 0 };

	line_t* line = side->linedef;
	bool isFront = side == line->sidedef[0];
	side_t* partnerSide = line->sidedef[isFront ? 1 : 0];

	// Treat two-sided lines that block rendering as one-sided cover to prevent sprites from bleeding through.
	if (partnerSide == nullptr || RenderBlock::IsBlocked(line, isFront))
		return { (float)ProjectHeight, (float)ProjectHeight };

	// Do not add to upper portion of lower textures, or upper portion of lower textures
	// Adjust cover wall offsets to not block extra pixels from the the backside
	switch (location)
	{
	case WallLocation::Upper:
		return { (float)ProjectHeight, -(float)Helion::World::WorldStatic::LineVertexGapBottomZ };
	case WallLocation::Lower:
		return { -(float)Helion::World::WorldStatic::LineVertexGapTopZ, (float)ProjectHeight };
	default:
		return { (float)ProjectHeight, (float)ProjectHeight };
	}
}

}

void CoverWallUtil::SetCoverWallVertices(side_t* side, DynamicVertex* vertices, int index, WallLocation location)
{
	Heights heights = GetProjectHeights(side, location);
	DynamicVertex* v = vertices + index;
	for (int i = 0; i < 6; i++, v++)
	{
		float delta = (i & 1) == 0 ? heights.AddTop : -heights.SubBottom;
		v->Z += delta;
		v->PrevZ += delta;
	}
}

void CoverWallUtil::CopyCoverWallVertices(side_t* side, StaticVertex* staticVertices, std::span<const DynamicVertex> vertices, int index, WallLocation location)
{
	Heights heights = GetProjectHeights(side, location);
	const DynamicVertex* v = vertices.data();
	for (int i = 0; i < 6; i++, v++)
	{
		float delta = (i & 1) == 0 ? heights.AddTop : -heights.SubBottom;
		StaticVertex& sv = staticVertices[index++];
		sv = StaticVertex(v->X, v->Y, v->Z + delta, v->U, v->V, v->SurfaceOptions, v->LightLevelAdd, 0);
		sv.LightmapU = v->LightmapU;
		sv.LightmapV = v->LightmapV;
		sv.LightmapIndex = v->LightmapIndex;
	}
}

}
