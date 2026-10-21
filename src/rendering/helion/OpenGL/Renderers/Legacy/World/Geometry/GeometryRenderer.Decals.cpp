/*
** GeometryRenderer.Decals.cpp
**
** UZDoom-only (plan M4.5): the wall slices DecalRenderer projects the decals of a side on. HWWall::PutWall runs
** ProcessDecals for the solid walls it emits (RENDERWALL_TOP / BOTTOM / M1S and the non-masked RENDERWALL_FFBLOCK);
** Helion caches the wall vertices of static geometry, so the decals are gathered per frame from the sides that carry
** decals instead, with the same slice heights the wall code uses (the planes at the line's vertices, the fake contrast
** of the side, the sky hack that drops the upper / lower part between two sky planes).
**
**---------------------------------------------------------------------------
**
** Copyright 2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/DecalRenderer.h"
#include "g_levellocals.h"
#include "r_defs.h"
#include "r_sky.h"
#include "texturemanager.h"
#include "gametexture.h"
#include "a_sharedglobal.h"
#include "Renderer.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

using ::Helion::World::Geometry::Walls::WallLocation;

void GeometryRenderer::RenderSideDecals(side_t* side, DecalRenderer& decalRenderer, Shared::RenderInfo& renderInfo)
{
	line_t* line = side->linedef;
	if (line == nullptr || side->AttachedDecals == nullptr || side->sector == nullptr)
		return;

	const bool isFront = line->sidedef[0] == side;
	vertex_t* v1 = isFront ? line->v1 : line->v2;
	vertex_t* v2 = isFront ? line->v2 : line->v1;
	sector_t* front = side->sector;
	sector_t* back = line->sidedef[1] == nullptr ? nullptr : (isFront ? line->backsector : line->frontsector);

	DecalWall wall;
	wall.Side = side;
	wall.FrontSector = front;
	wall.BackSector = back;
	wall.X1 = (float)v1->fX();
	wall.Y1 = (float)v1->fY();
	wall.X2 = (float)v2->fX();
	wall.Y2 = (float)v2->fY();
	wall.FracLeft = 0;
	wall.FracRight = 1;
	wall.LightLevelOffset = (float)GetLightLevelAdd(side);

	const DVector2 p1(v1->fX(), v1->fY());
	const DVector2 p2(v2->fX(), v2->fY());
	const float frontCeil1 = (float)front->ceilingplane.ZatPoint(p1), frontCeil2 = (float)front->ceilingplane.ZatPoint(p2);
	const float frontFloor1 = (float)front->floorplane.ZatPoint(p1), frontFloor2 = (float)front->floorplane.ZatPoint(p2);

	auto emit = [&](WallLocation location, int tier, float top1, float top2, float bottom1, float bottom2)
	{
		if (top1 <= bottom1 && top2 <= bottom2)
			return;
		int overrideLightIndex = 0;
		wall.Location = location;
		wall.LightBufferIndex = ::Helion::Render::Renderer::GetLightBufferIndex(side, tier, front, overrideLightIndex);
		wall.ZTopLeft = top1;
		wall.ZTopRight = top2;
		wall.ZBottomLeft = bottom1;
		wall.ZBottomRight = bottom2;
		decalRenderer.RenderDecals(wall, renderInfo);
	};

	if (back == nullptr)
	{
		// RENDERWALL_M1S. RENDERWALL_MIRRORSURFACE (Mirror, drawn by RenderMirrorDecals) belongs to the mirror portal, which is not
		// part of the build yet: a mirror line is a plain wall and keeps its decals here.
		emit(WallLocation::Middle, side_t::mid, frontCeil1, frontCeil2, frontFloor1, frontFloor2);
		return;
	}

	const float backCeil1 = (float)back->ceilingplane.ZatPoint(p1), backCeil2 = (float)back->ceilingplane.ZatPoint(p2);
	const float backFloor1 = (float)back->floorplane.ZatPoint(p1), backFloor2 = (float)back->floorplane.ZatPoint(p2);

	// RENDERWALL_TOP: not between two sky ceilings (the sky hack).
	if (!(front->GetTexture(sector_t::ceiling) == skyflatnum && back->GetTexture(sector_t::ceiling) == skyflatnum))
		emit(WallLocation::Upper, side_t::top, frontCeil1, frontCeil2, std::max(backCeil1, frontFloor1), std::max(backCeil2, frontFloor2));

	// RENDERWALL_BOTTOM
	if (!(front->GetTexture(sector_t::floor) == skyflatnum && back->GetTexture(sector_t::floor) == skyflatnum))
		emit(WallLocation::Lower, side_t::bottom, std::min(backFloor1, frontCeil1), std::min(backFloor2, frontCeil2), frontFloor1, frontFloor2);

	// RENDERWALL_FFBLOCK: the sides of the 3D floors of the back sector, clipped to the front sector.
	if (back->e == nullptr)
		return;

	for (F3DFloor* rover : back->e->XFloor.ffloors)
	{
		if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_RENDERSIDES) || rover->model == nullptr)
			continue;
		if (rover->flags & FF_THISINSIDE)
			continue;

		side_t* textureSide = (rover->flags & FF_UPPERTEXTURE) ? side : (rover->flags & FF_LOWERTEXTURE) ? side : rover->master->sidedef[0];
		const int textureTier = (rover->flags & FF_UPPERTEXTURE) ? side_t::top : (rover->flags & FF_LOWERTEXTURE) ? side_t::bottom : side_t::mid;
		FGameTexture* texture = TexMan.GetGameTexture(textureSide->GetTexture(textureTier), true);
		wall.MaskedTexture = texture == nullptr || texture->isMasked() || (rover->flags & (FF_TRANSLUCENT | FF_ADDITIVETRANS)) != 0;

		const float top1 = std::min((float)rover->top.plane->ZatPoint(p1), frontCeil1);
		const float top2 = std::min((float)rover->top.plane->ZatPoint(p2), frontCeil2);
		const float bottom1 = std::max((float)rover->bottom.plane->ZatPoint(p1), frontFloor1);
		const float bottom2 = std::max((float)rover->bottom.plane->ZatPoint(p2), frontFloor2);
		emit(WallLocation::Middle3D, side_t::mid, top1, top2, bottom1, bottom2);
	}
}

}
