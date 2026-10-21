/*
** MidTextureHack.cpp
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
*/

#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/MidTextureHack.h"
#include "Common/Shared/World/WorldTriangulator.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"
#include "World/Static/RenderStaticState.h"
#include "g_levellocals.h"
#include "r_defs.h"
#include "r_sky.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

using Helion::Render::OpenGL::Shared::World::WorldTriangulator;
using Helion::Render::OpenGL::Shared::World::WallVertices;
using Helion::World::Geometry::Sectors::RenderSector;
using Helion::World::Geometry::Sectors::SectorPlaneFace;
using Helion::World::Geometry::Sectors::SectorPlanes;
using Helion::World::Static::RenderStaticState;

namespace
{

// [UZDoom] State that Helion keeps on its world objects.
FLevelLocals* s_level = nullptr;
std::vector<uint8_t> s_noRender; // sector index * 2 + plane
struct SectorBoxEntry
{
	double MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
	bool Valid = false;
};
std::vector<SectorBoxEntry> s_boxes;

bool IsNoTexture(FTextureID id)
{
	return id.GetIndex() <= 0;
}

// Box.Contains(point) of Helion is exclusive.
bool BoxContainsPoint(const SectorBoxEntry& box, double x, double y)
{
	return x > box.MinX && x < box.MaxX && y > box.MinY && y < box.MaxY;
}

}

void MidTextureHack::Clear()
{
	s_level = nullptr;
	s_noRender.clear();
	s_boxes.clear();
}

bool MidTextureHack::PlaneNoRender(const sector_t* sector, int pos)
{
	size_t index = (size_t)sector->Index() * 2 + pos;
	return index < s_noRender.size() && s_noRender[index] != 0;
}

bool MidTextureHack::PlaneNoRenderOrFlag(const sector_t* sector, int pos)
{
	return (sector->GetFlags(pos) & PLANEF_NORENDER) != 0 || PlaneNoRender(sector, pos);
}

void MidTextureHack::PrepareLevel(FLevelLocals* world)
{
	size_t count = world->sectors.Size();
	if (s_level == world && s_boxes.size() == count)
		return;

	s_level = world;
	s_noRender.assign(count * 2, 0);
	s_boxes.assign(count, SectorBoxEntry());

	// [UZDoom] Replaces the island boxes: one box per sector over the vertices of its lines.
	for (size_t i = 0; i < count; i++)
	{
		sector_t& sector = world->sectors[(unsigned)i];
		SectorBoxEntry& box = s_boxes[i];
		for (unsigned l = 0; l < sector.Lines.Size(); l++)
		{
			line_t* line = sector.Lines[l];
			vertex_t* vertices[2] = { line->v1, line->v2 };
			for (vertex_t* v : vertices)
			{
				double x = v->fX();
				double y = v->fY();
				if (!box.Valid)
				{
					box.MinX = box.MaxX = x;
					box.MinY = box.MaxY = y;
					box.Valid = true;
					continue;
				}

				box.MinX = std::min(box.MinX, x);
				box.MaxX = std::max(box.MaxX, x);
				box.MinY = std::min(box.MinY, y);
				box.MaxY = std::max(box.MaxY, y);
			}
		}
	}
}

void MidTextureHack::ClearApplyState()
{
	m_floodLineSet.clear();
	m_midTextureHackLines.clear();
	m_midTextureHackSectors.clear();
}

void MidTextureHack::Apply(FLevelLocals* world, Texture::Legacy::LegacyGLTextureManager* glTextureManager, GeometryRenderer& geometryRenderer)
{
	PrepareLevel(world);
	for (unsigned i = 0; i < world->sectors.Size(); i++)
		ApplyToSector(world, glTextureManager, geometryRenderer, &world->sectors[i]);

	ClearApplyState();
}

void MidTextureHack::Apply(FLevelLocals* world, const std::vector<int>& sectorIds, Texture::Legacy::LegacyGLTextureManager* glTextureManager,
	GeometryRenderer& geometryRenderer)
{
	PrepareLevel(world);
	for (int sectorId : sectorIds)
	{
		if (sectorId >= 0 && (unsigned)sectorId < world->sectors.Size())
			ApplyToSector(world, glTextureManager, geometryRenderer, &world->sectors[sectorId]);
	}

	ClearApplyState();
}

void MidTextureHack::ApplyToSector(FLevelLocals* world, Texture::Legacy::LegacyGLTextureManager* glTextureManager, GeometryRenderer& geometryRenderer,
	sector_t* sector)
{
	if (sector->GetHeightSec() != nullptr)
		return;

	bool clippedFloor = false;
	bool clippedCeiling = false;
	double sectorFloorZ = sector->GetPlaneTexZ(sector_t::floor);
	double sectorCeilingZ = sector->GetPlaneTexZ(sector_t::ceiling);

	for (unsigned i = 0; i < sector->Lines.Size(); i++)
	{
		line_t* line = sector->Lines[i];
		if (m_midTextureHackLines.contains(line->Index()))
			continue;

		if (line->sidedef[1] == nullptr)
			continue;

		// UZDoom: Helion compares the front middle texture with itself here. A line with no middle texture on either side
		// has nothing to clip with, which is what both checks resolve to.
		bool frontNone = IsNoTexture(line->sidedef[0]->GetTexture(side_t::mid));
		bool backNone = IsNoTexture(line->sidedef[1]->GetTexture(side_t::mid));
		if (frontNone && backNone)
			continue;

		side_t* side = frontNone ? line->sidedef[1] : line->sidedef[0];
		auto* texture = glTextureManager->GetTexture(side->GetTexture(side_t::mid).GetIndex());
		if (texture == nullptr)
			continue;

		// Do not emulate unless there are transparent pixels or alpha.
		if (texture->TransparentPixelCount == 0 && line->alpha >= 1)
			continue;

		m_midTextureHackLines.insert(line->Index());
		side_t* partnerSide = side == line->sidedef[0] ? line->sidedef[1] : line->sidedef[0];
		auto opening = GeometryRenderer::GetMidTexOpening(side, RenderSector::From(side->sector), RenderSector::From(partnerSide->sector), false);

		WallVertices wall;
		bool nothingVisible = false;
		WorldTriangulator::HandleTwoSidedMiddle(side, texture->Dimension(), texture->UVInverse, opening, opening,
			side == line->sidedef[0], wall, nothingVisible, 0, 0, SectorPlanes::None);

		if (wall.BottomRight.Z < sectorFloorZ && line->frontsector->GetPlaneTexZ(sector_t::floor) == line->backsector->GetPlaneTexZ(sector_t::floor))
			clippedFloor = true;

		if (wall.TopLeft.Z > sectorCeilingZ && line->frontsector->GetPlaneTexZ(sector_t::ceiling) == line->backsector->GetPlaneTexZ(sector_t::ceiling))
			clippedCeiling = true;
	}

	if (clippedFloor)
		SetSectorLinesForMidTextureHack(world, geometryRenderer, sector, SectorPlaneFace::Floor);

	if (clippedCeiling)
		SetSectorLinesForMidTextureHack(world, geometryRenderer, sector, SectorPlaneFace::Ceiling);

	if (clippedFloor || clippedCeiling)
		m_midTextureHackSectors.insert(sector->Index());

	SetSectorForMidTextureHack(sector, clippedFloor, clippedCeiling);
	FindContainingFloodSectors(world, sector, m_containingSectors, clippedFloor, clippedCeiling);

	for (sector_t* containingSector : m_containingSectors)
		SetSectorForMidTextureHack(containingSector, clippedFloor, clippedCeiling);

	m_containingSectors.clear();
	m_midTextureHackLines.clear();
}

void MidTextureHack::SetSectorForMidTextureHack(sector_t* sector, bool clippedFloor, bool clippedCeiling)
{
	auto& floor = RenderStaticState::Plane(sector, sector_t::floor);
	auto& ceiling = RenderStaticState::Plane(sector, sector_t::ceiling);
	floor.MidTextureHack = floor.MidTextureHack || clippedFloor;
	ceiling.MidTextureHack = ceiling.MidTextureHack || clippedCeiling;

	size_t index = (size_t)sector->Index() * 2;
	if (index + 1 < s_noRender.size())
	{
		if (clippedFloor)
			s_noRender[index + sector_t::floor] = 1;
		if (clippedCeiling)
			s_noRender[index + sector_t::ceiling] = 1;
	}
}

void MidTextureHack::FindContainingFloodSectors(FLevelLocals* world, sector_t* sector, std::vector<sector_t*>& sectors, bool floor, bool ceiling)
{
	int searchId = sector->Index();
	if ((size_t)searchId >= s_boxes.size() || !s_boxes[searchId].Valid)
		return;

	const SectorBoxEntry& searchBox = s_boxes[searchId];
	for (unsigned sectorId = 0; sectorId < world->sectors.Size(); sectorId++)
	{
		if ((int)sectorId == searchId)
			continue;

		// Only flood matching planes
		sector_t* checkSector = &world->sectors[sectorId];
		if (floor && (checkSector->GetPlaneTexZ(sector_t::floor) != sector->GetPlaneTexZ(sector_t::floor) ||
			checkSector->GetTexture(sector_t::floor) != sector->GetTexture(sector_t::floor)))
			continue;

		if (ceiling && (checkSector->GetPlaneTexZ(sector_t::ceiling) != sector->GetPlaneTexZ(sector_t::ceiling) ||
			checkSector->GetTexture(sector_t::ceiling) != sector->GetTexture(sector_t::ceiling)))
			continue;

		const SectorBoxEntry& checkBox = s_boxes[sectorId];
		if (!checkBox.Valid)
			continue;

		if (BoxContainsPoint(searchBox, checkBox.MinX, checkBox.MinY) && BoxContainsPoint(searchBox, checkBox.MaxX, checkBox.MaxY))
			sectors.push_back(checkSector);
	}
}

void MidTextureHack::SetSectorLinesForMidTextureHack(FLevelLocals* world, GeometryRenderer& geometryRenderer, sector_t* sector, SectorPlaneFace face)
{
	int pos = face == SectorPlaneFace::Floor ? sector_t::floor : sector_t::ceiling;
	for (unsigned i = 0; i < sector->Lines.Size(); i++)
	{
		line_t* line = sector->Lines[i];
		if (line->sidedef[1] != nullptr && line->frontsector == line->backsector)
			continue;

		if (PlaneNoRenderOrFlag(line->frontsector, pos) || sector->GetTexture(pos) == skyflatnum)
			continue;

		if (m_floodLineSet.contains(line->Index()))
			continue;

		if (m_midTextureHackSectors.contains(line->frontsector->Index()) ||
			(line->sidedef[1] != nullptr && m_midTextureHackSectors.contains(line->backsector->Index())))
			continue;

		side_t* facingSide = line->sidedef[1] == nullptr || line->frontsector == sector ? line->sidedef[0] : line->sidedef[1];
		if (line->sidedef[1] != nullptr && face == SectorPlaneFace::Floor && IsNoTexture(facingSide->GetTexture(side_t::bottom)))
			continue;

		m_floodLineSet.insert(line->Index());
		SectorPlanes current = RenderStaticState::Side(facingSide).MidTextureFlood;
		RenderStaticState::SetSideMidTextureFlood(facingSide, current | (face == SectorPlaneFace::Ceiling ? SectorPlanes::Ceiling : SectorPlanes::Floor));
	}
}

}
