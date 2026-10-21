/*
** SectorPlane3D.h
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

#pragma once

#include "World/Geometry/Sectors/RenderSector.h"

struct sector_t;

namespace Helion::World::Geometry::Sectors
{

class Sector3D;

enum class PlaneFace3D { Top, Bottom };

struct SectorPlane3D;

struct PlaneSortKey
{
	double Z = 0;
	double ControlTopZ = 0;
	bool IsSolid = true;
	bool IsOpaque = true;
	int ControlSectorId = 0;
	PlaneFace3D Face = PlaneFace3D::Top;

	PlaneSortKey() = default;
	explicit PlaneSortKey(const SectorPlane3D& p);

	// Helion IComparable.CompareTo
	int CompareTo(const PlaneSortKey& other) const;
};

// Helion SectorPlane3D(SectorPlane controlPlane, SectorPlane plane, Sector3D? sector3D, PlaneFace3D face, Sector lightSector).
// The planes are by-value snapshots (RenderPlane), refreshed by Sector3DTable::Update. LightSector/LightInsideSector
// are the sectors (real sector_t) whose light the plane uses.
struct SectorPlane3D
{
	int ControlSectorId = 0;
	RenderPlane ControlPlane;
	RenderPlane Plane;
	Sector3D* Sector3DPtr = nullptr;	// Helion SectorPlane3D.Sector3D (a member cannot share the type name in C++)
	PlaneFace3D Face = PlaneFace3D::Top;
	sector_t* LightSector = nullptr;
	sector_t* LightInsideSector = nullptr;
	bool NoRenderWall = false;
	PlaneSortKey SortKey;
	int CheckCount = 0;

	SectorPlane3D() = default;
	SectorPlane3D(const RenderPlane& controlPlane, const RenderPlane& plane, Sector3D* sector3D, PlaneFace3D face, sector_t* lightSector);

	double GetZ() const;
	double GetPrevZ() const;
	void UpdateSortKey() { SortKey = PlaneSortKey(*this); }
};

}
