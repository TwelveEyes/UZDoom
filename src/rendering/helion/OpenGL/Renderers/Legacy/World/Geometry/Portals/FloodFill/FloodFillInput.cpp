/*
** FloodFillInput.cpp
**
** UZDoom-only. See FloodFillInput.h. Port of the flood part of Helion.World.Static.StaticDataApplier.
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

#include "FloodFillInput.h"

#include <algorithm>
#include "g_levellocals.h"
#include "r_defs.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.h"
#include "World/Static/RenderStaticState.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals::FloodFill
{

using ::Helion::World::Geometry::Sectors::TransferHeightView;
using ::Helion::World::Static::RenderStaticState;

int FloodFillInput::SideTextureHandle(const side_t* side, int tier)
{
	return std::max(0, side->textures[tier].texture.GetIndex());
}

void FloodFillInput::DetermineFloodFill(FLevelLocals* world)
{
	for (unsigned i = 0; i < world->lines.Size(); i++)
		CheckFloodFill(&world->lines[i]);
}

void FloodFillInput::CheckFloodFill(line_t* line)
{
	if (line->sidedef[1] == nullptr)
		return;

	side_t* front = line->sidedef[0];
	side_t* back = line->sidedef[1];
	RenderSector frontSector = ::Helion::World::Geometry::Sectors::GetRenderSector(front->sector, TransferHeightView::Middle);
	RenderSector backSector = ::Helion::World::Geometry::Sectors::GetBackRenderSector(back->sector, TransferHeightView::Middle);
	SetFloodFillSide(front, back, frontSector, backSector);
	// The back side sees the front sector as its back sector (hw_FakeFlat back = true).
	SetFloodFillSide(back, front, ::Helion::World::Geometry::Sectors::GetRenderSector(back->sector, TransferHeightView::Middle),
		::Helion::World::Geometry::Sectors::GetBackRenderSector(front->sector, TransferHeightView::Middle));
}

// UZDoom (hw_renderhacks.cpp classic PrepareUnhandledMissingTextures): MAPINFO notexturefill disables the missing texture
// floods; a transparent door (transdoor), a sky or a portal on the other sector's plane never floods.
static bool MissingTextureFloodAllowed(side_t* otherSide, const ::Helion::World::Geometry::Sectors::RenderPlane& otherPlane, int planePos)
{
	sector_t* other = otherSide->sector;
	if (other->Level->notexturefill || other->transdoor)
		return false;
	if (GeometryRenderer::IsSkyTexture(otherPlane.TextureHandle))
		return false;
	return other->ValidatePortal(planePos) == nullptr;
}

bool FloodFillInput::ShouldFloodLower(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector)
{
	return SideTextureHandle(facingSide, side_t::bottom) <= 0 /* Constants.NullCompatibilityTextureIndex */ &&
		(facingSector.Floor.Z < otherSector.Floor.Z || facingSector.Floor.PrevZ < otherSector.Floor.PrevZ) &&
		MissingTextureFloodAllowed(otherSide, otherSector.Floor, sector_t::floor);
}

bool FloodFillInput::ShouldFloodUpper(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector)
{
	// UZDoom (hw_walls.cpp upper texture): a closed facing sector whose floor is above the other ceiling has no upper wall at all.
	if ((facingSide->linedef->flags & ML_DRAWFULLHEIGHT) == 0 && facingSector.Floor.Z > otherSector.Ceiling.Z &&
		facingSector.Floor.Z >= facingSector.Ceiling.Z && facingSector.Floor.PrevZ >= facingSector.Ceiling.PrevZ)
		return false;

	bool skyHack;
	return SideTextureHandle(facingSide, side_t::top) <= 1 &&
		(facingSector.Ceiling.Z > otherSector.Ceiling.Z || facingSector.Ceiling.PrevZ > otherSector.Ceiling.PrevZ) &&
		MissingTextureFloodAllowed(otherSide, otherSector.Ceiling, sector_t::ceiling) &&
		GeometryRenderer::UpperIsVisibleOrFlood(facingSide, otherSide, facingSector, otherSector, skyHack);
}

void FloodFillInput::SetFloodFillSide(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector)
{
	auto& state = RenderStaticState::Side(facingSide);
	if (ShouldFloodLower(facingSide, otherSide, facingSector, otherSector))
		state.FloodTextures |= SideTexture::Lower;
	else
		state.FloodTextures &= ~SideTexture::Lower;

	if (ShouldFloodUpper(facingSide, otherSide, facingSector, otherSector))
		state.FloodTextures |= SideTexture::Upper;
	else
		state.FloodTextures &= ~SideTexture::Upper;
}

void FloodFillInput::SetSectorFlood(sector_t* sector, bool flood)
{
	RenderStaticState::SetSectorFlood(sector, flood);
}

void FloodFillInput::SetSideMidTextureFlood(side_t* side, ::Helion::World::Geometry::Sectors::SectorPlanes planes)
{
	RenderStaticState::SetSideMidTextureFlood(side, planes);
}

void FloodFillInput::AddSideFloodTextures(side_t* side, SideTexture textures)
{
	RenderStaticState::Side(side).FloodTextures |= textures;
}

void FloodFillInput::ClearSideFloodTextures(side_t* side, SideTexture textures)
{
	RenderStaticState::Side(side).FloodTextures &= ~textures;
}

}
