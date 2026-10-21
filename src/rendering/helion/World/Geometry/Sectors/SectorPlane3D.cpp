/*
** SectorPlane3D.cpp
**
** Port of Helion.World.Geometry.Sectors.SectorPlane3D / PlaneSortKey.
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

#include "World/Geometry/Sectors/SectorPlane3D.h"
#include "World/Geometry/Sectors/Sector3D.h"
#include "r_defs.h"

namespace Helion::World::Geometry::Sectors
{

namespace
{

// C# double.CompareTo
int CompareDouble(double a, double b)
{
	return a < b ? -1 : (a > b ? 1 : 0);
}

// C# bool.CompareTo (false < true)
int CompareBool(bool a, bool b)
{
	return (int)a - (int)b;
}

}

SectorPlane3D::SectorPlane3D(const RenderPlane& controlPlane, const RenderPlane& plane, Sector3D* sector3D, PlaneFace3D face, sector_t* lightSector) :
	ControlSectorId(controlPlane.Sector->Index()),
	ControlPlane(controlPlane),
	Plane(plane),
	Sector3DPtr(sector3D),
	Face(face),
	LightSector(lightSector),
	LightInsideSector(lightSector)
{
}

double SectorPlane3D::GetZ() const
{
	return (Sector3DPtr != nullptr && Face == PlaneFace3D::Bottom)
		? Sector3DPtr->ClipBottomZ
		: Plane.Z;
}

double SectorPlane3D::GetPrevZ() const
{
	return (Sector3DPtr != nullptr && Face == PlaneFace3D::Bottom)
		? Sector3DPtr->ClipPrevBottomZ
		: Plane.PrevZ;
}

PlaneSortKey::PlaneSortKey(const SectorPlane3D& p)
{
	Z = p.GetZ();

	ControlTopZ = p.Sector3DPtr != nullptr ? p.Sector3DPtr->ControlTop.Z : p.Plane.Z;

	IsSolid = p.Sector3DPtr != nullptr ? p.Sector3DPtr->IsSolid() : true;
	IsOpaque = p.Sector3DPtr != nullptr ? p.Sector3DPtr->IsOpaque() : true;

	ControlSectorId = p.ControlSectorId;
	Face = p.Face;
}

int PlaneSortKey::CompareTo(const PlaneSortKey& other) const
{
	int cmp = CompareDouble(other.Z, Z);
	if (cmp != 0)
		return cmp;

	cmp = CompareDouble(other.ControlTopZ, ControlTopZ);
	if (cmp != 0)
		return cmp;

	cmp = CompareBool(IsOpaque, other.IsOpaque);
	if (cmp != 0)
		return cmp;

	cmp = CompareBool(IsSolid, other.IsSolid);
	if (cmp != 0)
		return cmp;

	cmp = ControlSectorId < other.ControlSectorId ? -1 : (ControlSectorId > other.ControlSectorId ? 1 : 0);
	if (cmp != 0)
		return cmp;

	return (int)Face - (int)other.Face;
}

}
