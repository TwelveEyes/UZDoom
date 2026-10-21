/*
** GeometryRenderer.FakeFlat.cpp
**
** UZDoom-only: Helion Sector.GetRenderSector(TransferHeightView) with the rules of hw_FakeFlat (hw_fakeflat.cpp).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Top / Middle / Bottom are hw_FakeFlat's area_above / area_normal / area_below. The sector is built on a copy of
** the real sector_t (the copy keeps sectornum, so RenderSector::Id is the real sector index: the FSection key; only the render data is used: planes, colors, light, TexZ), which carries the plane equations
** the triangulator needs for slopes. sector_t::e is never used through the copies. The copies are kept per
** (sector, view) and overwritten on every call, like Helion's ring of RenderSectors, so the result must be
** consumed before the next GetRenderSector call for the same sector and view.
** Not carried over from hw_FakeFlat: the VBO/IBO bookkeeping (iboindex/vboheight), which Helion's renderer does not have.
** The OVERLAPPING back sector copy is GetBackRenderSector (the "back" argument of hw_FakeFlat).
*/

#include <memory>
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.h"
#include "World/Geometry/Sectors/RenderSector.h"
#include "World/Geometry/Sectors/RenderSectorFakeFlat.h"
#include "g_levellocals.h"
#include "r_defs.h"
#include "r_sky.h"

namespace Helion::World::Geometry::Sectors
{

namespace
{

FLevelLocals* s_level = nullptr;
unsigned s_sectorCount = 0;
// 4 copies per sector: the Top, Middle, Bottom views and the OVERLAPPING back sector copy. sector_t is trivially copyable.
constexpr unsigned CopiesPerSector = 4;
constexpr unsigned OverlappingSlot = 3;
std::unique_ptr<sector_t[]> s_copies;

enum class Area
{
	Above,
	Normal,
	Below,
};

sector_t* GetCopy(sector_t* sec, unsigned slot)
{
	FLevelLocals* level = sec->Level;
	unsigned count = level->sectors.Size();
	if (s_copies == nullptr || s_level != level || s_sectorCount != count)
	{
		s_level = level;
		s_sectorCount = count;
		s_copies = std::make_unique<sector_t[]>((size_t)count * CopiesPerSector);
	}

	sector_t* dest = &s_copies[(size_t)sec->Index() * CopiesPerSector + slot];
	*dest = *sec;
	return dest;
}

// The light/flags override of a fake area that does not have NOFAKELIGHT.
void CopyFakeLight(sector_t* dest, sector_t* s, bool lightLevel)
{
	if (lightLevel)
		dest->lightlevel = s->lightlevel;
	dest->SetPlaneLight(sector_t::floor, s->GetPlaneLight(sector_t::floor));
	dest->SetPlaneLight(sector_t::ceiling, s->GetPlaneLight(sector_t::ceiling));
	dest->ChangeFlags(sector_t::floor, -1, s->GetFlags(sector_t::floor));
	dest->ChangeFlags(sector_t::ceiling, -1, s->GetFlags(sector_t::ceiling));
}

RenderSector ToRenderSector(sector_t* dest)
{
	RenderSector result = RenderSector::From(dest);
	result.TransferHeights = true;
	result.Floor.TransferHeights = true;
	result.Ceiling.TransferHeights = true;
	return result;
}

}

bool SectorFakeFlatApplies(sector_t* sector)
{
	return sector->heightsec != nullptr && !(sector->heightsec->MoreFlags & SECMF_IGNOREHEIGHTSEC);
}

TransferHeightView GetTransferHeightView(sector_t* viewSector, const DVector3& viewPos)
{
	if (!SectorFakeFlatApplies(viewSector))
		return TransferHeightView::Middle;

	sector_t* s = viewSector->heightsec;
	if (viewPos.Z <= s->floorplane.ZatPoint(viewPos))
		return TransferHeightView::Bottom;
	if (viewPos.Z > s->ceilingplane.ZatPoint(viewPos) && !(s->MoreFlags & SECMF_FAKEFLOORONLY))
		return TransferHeightView::Top;
	return TransferHeightView::Middle;
}

RenderSector GetRenderSector(sector_t* sec, TransferHeightView view)
{
	sector_t* s = sec->GetHeightSec();
	if (s == nullptr || s == sec)
		return RenderSector::From(sec);

	Area area = view == TransferHeightView::Top ? Area::Above : view == TransferHeightView::Bottom ? Area::Below : Area::Normal;
	if (area == Area::Above)
	{
		if (s->MoreFlags & SECMF_FAKEFLOORONLY)
			area = Area::Normal;
	}

	bool diffTex = (s->MoreFlags & SECMF_CLIPFAKEPLANES) != 0;
	bool noFakeLight = (s->MoreFlags & SECMF_NOFAKELIGHT) != 0;
	sector_t* dest = GetCopy(sec, (unsigned)view);

	// Replace floor and ceiling height with control sector's heights.
	if (diffTex)
	{
		if (s->floorplane.CopyPlaneIfValid(&dest->floorplane, &sec->ceilingplane))
		{
			dest->planes[sector_t::floor].Texture = s->GetTexture(sector_t::floor);
			dest->SetPlaneTexZQuick(sector_t::floor, s->GetPlaneTexZ(sector_t::floor));
		}
		else if (s->MoreFlags & SECMF_FAKEFLOORONLY)
		{
			if (area == Area::Below)
			{
				dest->CopyColors(s);
				if (!noFakeLight)
					CopyFakeLight(dest, s, true);
				return ToRenderSector(dest);
			}
			return RenderSector::From(sec);
		}
	}
	else
	{
		dest->SetPlaneTexZQuick(sector_t::floor, s->GetPlaneTexZ(sector_t::floor));
		dest->floorplane = s->floorplane;
	}

	if (!(s->MoreFlags & SECMF_FAKEFLOORONLY))
	{
		if (diffTex)
		{
			if (s->ceilingplane.CopyPlaneIfValid(&dest->ceilingplane, &sec->floorplane))
			{
				dest->planes[sector_t::ceiling].Texture = s->GetTexture(sector_t::ceiling);
				dest->SetPlaneTexZQuick(sector_t::ceiling, s->GetPlaneTexZ(sector_t::ceiling));
			}
		}
		else
		{
			dest->ceilingplane = s->ceilingplane;
			dest->SetPlaneTexZQuick(sector_t::ceiling, s->GetPlaneTexZ(sector_t::ceiling));
		}
	}

	if (area == Area::Below)
	{
		dest->CopyColors(s);
		dest->SetPlaneTexZQuick(sector_t::floor, sec->GetPlaneTexZ(sector_t::floor));
		dest->SetPlaneTexZQuick(sector_t::ceiling, s->GetPlaneTexZ(sector_t::floor));
		dest->floorplane = sec->floorplane;
		dest->ceilingplane = s->floorplane;
		dest->ceilingplane.FlipVert();

		dest->ClearPortal(sector_t::ceiling);

		if (!noFakeLight)
		{
			dest->lightlevel = s->lightlevel;
		}

		dest->planes[sector_t::floor].Texture = diffTex ? sec->GetTexture(sector_t::floor) : s->GetTexture(sector_t::floor);
		dest->planes[sector_t::floor].xform = s->planes[sector_t::floor].xform;

		if (s->GetTexture(sector_t::ceiling) == skyflatnum)
		{
			dest->planes[sector_t::ceiling].Texture = dest->GetTexture(sector_t::floor);
			dest->planes[sector_t::ceiling].xform = dest->planes[sector_t::floor].xform;
		}
		else
		{
			dest->planes[sector_t::ceiling].Texture = diffTex ? s->GetTexture(sector_t::floor) : s->GetTexture(sector_t::ceiling);
			dest->planes[sector_t::ceiling].xform = s->planes[sector_t::ceiling].xform;
		}

		if (!noFakeLight)
			CopyFakeLight(dest, s, false);
	}
	else if (area == Area::Above)
	{
		dest->CopyColors(s);
		dest->SetPlaneTexZQuick(sector_t::ceiling, sec->GetPlaneTexZ(sector_t::ceiling));
		dest->SetPlaneTexZQuick(sector_t::floor, s->GetPlaneTexZ(sector_t::ceiling));
		dest->ceilingplane = sec->ceilingplane;
		dest->floorplane = s->ceilingplane;
		dest->floorplane.FlipVert();

		dest->ClearPortal(sector_t::floor);

		if (!noFakeLight)
		{
			dest->lightlevel = s->lightlevel;
		}

		dest->planes[sector_t::ceiling].Texture = diffTex ? sec->GetTexture(sector_t::ceiling) : s->GetTexture(sector_t::ceiling);
		dest->planes[sector_t::floor].Texture = s->GetTexture(sector_t::ceiling);
		dest->planes[sector_t::ceiling].xform = dest->planes[sector_t::floor].xform = s->planes[sector_t::ceiling].xform;

		if (s->GetTexture(sector_t::floor) != skyflatnum)
		{
			dest->planes[sector_t::floor].Texture = s->GetTexture(sector_t::floor);
			dest->planes[sector_t::floor].xform = s->planes[sector_t::floor].xform;
		}

		if (!noFakeLight)
			CopyFakeLight(dest, s, true);
	}

	return ToRenderSector(dest);
}

RenderSector GetBackRenderSector(sector_t* sec, TransferHeightView view)
{
	sector_t* s = sec->GetHeightSec();
	if (s != nullptr && s != sec)
		return GetRenderSector(sec, view);

	// hw_FakeFlat(back = true): a back sector with the ceiling lower than the floor gets its floor moved to the ceiling,
	// otherwise the upper and lower textures of the line overlap.
	if (!(sec->MoreFlags & SECMF_OVERLAPPING))
		return RenderSector::From(sec);

	sector_t* dest = GetCopy(sec, OverlappingSlot);
	dest->floorplane = sec->ceilingplane;
	dest->floorplane.FlipVert();
	dest->planes[sector_t::floor].TexZ = dest->planes[sector_t::ceiling].TexZ;
	dest->ClearPortal(sector_t::ceiling);
	dest->ClearPortal(sector_t::floor);
	return RenderSector::From(dest);
}

void ClearFakeFlatSectors()
{
	s_copies.reset();
	s_level = nullptr;
	s_sectorCount = 0;
}

}
