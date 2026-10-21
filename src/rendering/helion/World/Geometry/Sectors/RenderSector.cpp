/*
** RenderSector.cpp
**
** UZDoom-only: RenderSector / RenderPlane construction.
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

#include "World/Geometry/Sectors/RenderSector.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/MidTextureHack.h"
#include "World/Static/RenderStaticState.h"
#include "g_levellocals.h"
#include "r_defs.h"
#include "r_data/r_interpolate.h"

namespace Helion::World::Geometry::Sectors
{

RenderPlane RenderPlane::From(sector_t* sector, SectorPlaneFace face)
{
	int pos = face == SectorPlaneFace::Floor ? sector_t::floor : sector_t::ceiling;
	RenderPlane plane;
	static_cast<SectorPlane&>(plane) = SectorPlane::FromSector(sector, face);
	plane.PrevZ = PrevPlaneTexZ(sector, pos, plane.Z);
	plane.TextureHandle = sector->GetTexture(pos).GetIndex();
	plane.LightLevelAdd = sector->GetPlaneLight(pos);
	plane.LightLevelAbsolute = (sector->GetFlags(pos) & PLANEF_ABSLIGHTING) != 0;
	// PLANEF_NORENDER plus the NoRender bit of the mid texture hack (MidTextureHack::PlaneNoRender).
	plane.NoRender = (sector->GetFlags(pos) & PLANEF_NORENDER) != 0 ||
		Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::MidTextureHack::PlaneNoRender(sector, pos);
	plane.MidTextureHack = Helion::World::Static::RenderStaticState::Plane(sector, pos).MidTextureHack;
	plane.Id = sector->Index() * 2 + pos;
	return plane;
}

RenderSector RenderSector::From(sector_t* sector)
{
	RenderSector result;
	result.Sector = sector;
	result.Id = sector->Index();
	result.Floor = RenderPlane::From(sector, SectorPlaneFace::Floor);
	result.Ceiling = RenderPlane::From(sector, SectorPlaneFace::Ceiling);
	result.FloorSkyTextureHandle = sector->planes[sector_t::floor].skytexture[0].GetIndex();
	result.CeilingSkyTextureHandle = sector->planes[sector_t::ceiling].skytexture[0].GetIndex();
	result.LightLevel = sector->lightlevel;
	result.TransferHeights = sector->GetHeightSec() != nullptr;
	result.TransferFloorLightSectorId = result.Id;
	result.TransferCeilingLightSectorId = result.Id;
	return result;
}

double PrevPlaneTexZ(sector_t* sector, int pos, double currentZ)
{
	// Fake-flat copies (GetRenderSector with transfer heights) keep the real sector's index and interpolation
	// pointers but may carry the planes of the heightsec: they are not interpolated.
	if (sector->Level == nullptr || (unsigned)sector->Index() >= sector->Level->sectors.Size() || &sector->Level->sectors[sector->Index()] != sector)
		return currentZ;

	double oldTexZ;
	if (R_GetPlaneInterpolationOldTexZ(sector, pos, oldTexZ))
		return oldTexZ;
	return currentZ;
}

}
