/*
** StaticCacheGeometryRenderer.Sector3D.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Static.StaticCacheGeometryRenderer.Sector3D.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The fake sector / fake lines of Helion are the parent sector and its lines here (see StaticCacheGeometryRenderer.h):
** "for each line of the fake sector" is "for each line of the parent sector", and the static data of the wall of the
** fake side is RenderStaticState::Wall3D(Sector3D::SectorId, side, Middle3D).
** Not ported: Helion swaps ParentSector.TransferFloorLightSector to the parent sector while the planes are added; the
** light sector of the plane is passed to RenderSectorFlats as lightLevelSector (LightTop / LightBottom), which is what
** that swap works around.
**
*/

#include "StaticCacheGeometryRenderer.h"

#include "r_defs.h"
#include "World/Static/StaticDataApplier.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

using ::Helion::World::Static::RenderStaticState;

void StaticCacheGeometryRenderer::AddSectors3D(sector_t* sector, bool update)
{
	for (auto& sector3D : m_geometryRenderer.Sector3DTable().Sectors3D(sector))
		AddSector3D(sector3D.get(), SectorPlanes::Floor | SectorPlanes::Ceiling, update);
}

void StaticCacheGeometryRenderer::AddSector3D(Sector3D* sector3D, SectorPlanes planes, bool update, bool clearWallVertices)
{
	AddSectorPlanes3D(sector3D, planes, update);

	if (!sector3D->ShouldRenderWalls())
		return;

	sector_t* parent = sector3D->ParentSector;
	sector_t* control = sector3D->ControlSector;
	m_geometryRenderer.SetSectorForLineRendering3D(sector3D);
	for (unsigned i = 0; i < parent->Lines.Size(); i++)
	{
		// dynamic = side.IsDynamic || sector3D.ControlSector.IsMoving
		bool dynamic = ::Helion::World::Static::StaticDataApplier::IsMoving(control);
		if (dynamic && (control->renderDynamic[sector_t::floor] == SD_Movement || control->renderDynamic[sector_t::ceiling] == SD_Movement))
			continue;

		if (clearWallVertices)
		{
			line_t* line = parent->Lines[i];
			for (side_t* side : line->sidedef)
			{
				if (side == nullptr)
					continue;

				StaticGeometryData& wall = RenderStaticState::Wall3D(sector3D->SectorId, side, (int)WallLocation::Middle3D);
				ClearGeometryVertices(wall);

				if (wall.GeometryData != nullptr)
					m_freeManager.Add(wall, m_geometryRenderer.GetWallType(side, WallLocation::Middle3D, sector3D), GetRepeatY(side, WallLocation::Middle3D));

				wall.GeometryData = nullptr;
			}
		}

		m_geometryRenderer.RenderSectorLine3D(sector3D, (int)i, true, true, m_renderSectorWallVertices3D);
	}
}

// Ceiling = ControlTop
// Floor = ControlBottom
void StaticCacheGeometryRenderer::AddSectorPlanes3D(Sector3D* sector3D, SectorPlanes planes, bool update)
{
	planes = planes & sector3D->RenderPlanes;
	if (!sector3D->ShouldRenderFlats() || planes == SectorPlanes::None)
		return;

	// Light sectors: Helion sets ParentSector.TransferFloorLightSector = ParentSector while the planes are added (restored after).
	// The port never writes sector_t: RenderFlat skips the TransferFloorLightSector lookup for 3D planes (generateSector3D) and the
	// light comes from the explicit light level sector instead. Note the swap: the ControlTop surface (the top of the 3D floor, drawn
	// with FakeBottom as geometry plane) takes LightTop, the light of the slice above it; the ControlBottom surface (drawn with FakeTop)
	// takes LightBottom, the light of the slice below it (Sector3DTable::SetLight). GeometryRenderer::RenderSectorFlats3D does the same.
	sector_t* parent = sector3D->ParentSector;
	RenderSector lightTop;
	RenderSector lightBottom;
	if (sector3D->LightTop != nullptr)
		lightTop = RenderSector(sector3D->LightTop);
	if (sector3D->LightBottom != nullptr)
		lightBottom = RenderSector(sector3D->LightBottom);
	const RenderSector* lightTopPtr = sector3D->LightTop != nullptr ? &lightTop : nullptr;
	const RenderSector* lightBottomPtr = sector3D->LightBottom != nullptr ? &lightBottom : nullptr;

	if ((planes & SectorPlanes::Ceiling) != SectorPlanes::None)
	{
		// Render FakeBottom at ControlTop
		AddSectorPlane(parent, sector3D->ControlTop.Facing, true, update, nullptr, lightTopPtr, &sector3D->FakeBottom, true, sector3D, &sector3D->ControlTop);

		if (sector3D->HasFlipped)
			AddSectorPlane(parent, sector3D->ControlTop.Facing, false, update, nullptr, lightTopPtr, &sector3D->FakeBottomFlipped, true, sector3D, &sector3D->ControlTop);
	}

	if ((planes & SectorPlanes::Floor) != SectorPlanes::None)
	{
		// Render FakeTop at ControlBottom
		AddSectorPlane(parent, sector3D->ControlBottom.Facing, false, update, nullptr, lightBottomPtr, &sector3D->FakeTop, true, sector3D, &sector3D->ControlBottom);

		if (sector3D->HasFlipped)
			AddSectorPlane(parent, sector3D->ControlBottom.Facing, true, update, nullptr, lightBottomPtr, &sector3D->FakeTopFlipped, true, sector3D, &sector3D->ControlBottom);
	}
}

void StaticCacheGeometryRenderer::RenderSectorWallVertices3D(side_t* side, WallLocation wall, const RenderSector& wallSector, GLLegacyTexture* texture,
	DynamicVertexSpan vertices, Sector3D* sector3D)
{
	StaticGeometryData& wallStatic = RenderStaticState::Wall3D(sector3D->SectorId, side, (int)wall);
	UpdateVertices(wallStatic, TextureHandleOf(texture), vertices, nullptr, side, wall, true, texture, sector3D);
}

}
