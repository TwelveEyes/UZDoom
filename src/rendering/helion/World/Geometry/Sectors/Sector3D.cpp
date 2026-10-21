/*
** Sector3D.cpp
**
** Port of Helion.World.Geometry.Sectors.Sector3D (+ SetHeights3D from Sector3D.cs) as an adapter over UZDoom 3D floors.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** UZDoom mapping notes (see also Sector3D.h):
**  - One Sector3D per F3DFloor with FF_EXISTS and without FF_THISINSIDE (the software-renderer hack).
**  - ControlTop/ControlBottom are RenderPlane snapshots of F3DFloor::top/bottom (model sector + isceiling), the
**    same plane hw_flats.cpp feeds to HWFlat::Process. Their SectorPlane::Sector is the model sector and Facing
**    is the real isceiling, so SectorPlane::Plane() returns the model's secplane_t (slopes are kept).
**  - FakeTop/FakeBottom copy the control snapshots with Z/PrevZ replaced by the clipped WallHeights. The flat
**    offsets come from the same plane that provides the texture (Helion takes them from the opposite control
**    plane; UZDoom's top/bottom textures are model->planes[isceiling], so offsets must match).
**  - Helion FakeSectorFlipped.FakeBottomFlipped never receives its Z (CalculateHeights assigns FakeBottom
**    twice). That leaves the flipped floor at Z=0, so it is assigned here.
**  - Flag mapping from FF_*: FF_SOLID->Solid, FF_SWIMMABLE->Swim, FF_ALLSIDES|FF_INVERTSIDES->RenderInside,
**    FF_SEETHROUGH->SightInvert and FF_SHOOTTHROUGH->ShootInvert (solid floors only), FF_NOSHADE->DisableLighting
**    (the plane never becomes the carried light sector), FF_DOUBLESHADOW->RestrictLighting (light reset after
**    the floor), FF_RESET->ResetLight, FF_FOG->Fog (alpha 0), FF_FADEWALLS->NoViewFade, FF_UPPERTEXTURE/
**    FF_LOWERTEXTURE->UseParent*Texture, FF_ADDITIVETRANS->AdditiveTransparency, a floor with neither
**    FF_RENDERSIDES nor FF_RENDERPLANES->NoRender (+LightTransfer unless FF_NOSHADE: it only affects light).
**    FF_RENDERPLANES cleared leaves RenderPlanes empty, FF_RENDERSIDES cleared clears RenderSides.
**  - Limits: FF_INVERTPLANES/FF_BOTHPLANES (plane visibility from the other side), FF_FLOOD and FF_FIX (flood
**    towards another floor / floor of the real sector as ceiling) are approximated as ordinary floors, like
**    the plain plane/wall clipping of Helion. Sector.TransferHeights is not nulled for sectors with 3D floors
**    (sector_t::GetHeightSec cannot be cleared); ControlSector.IgnoreFogColor has no UZDoom counterpart.
*/

#include <algorithm>
#include <cfloat>
#include <cmath>
#include "World/Geometry/Sectors/Sector3D.h"
#include "Common/Shared/World/WorldTriangulator.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.h"
#include "g_levellocals.h"
#include "p_3dfloors.h"
#include "r_defs.h"

namespace Helion::World::Geometry::Sectors
{

using Render::OpenGL::Renderers::Legacy::World::Data::RenderDataStyle;
using Render::OpenGL::Renderers::Legacy::World::Geometry::GeometryRenderer;
using Render::OpenGL::Shared::World::WallVertices;
using Render::OpenGL::Shared::World::WorldTriangulator;

namespace
{

// Helion Side.PartnerSide
side_t* PartnerSide(side_t* side)
{
	line_t* line = side->linedef;
	if (line->sidedef[1] == nullptr)
		return nullptr;
	return line->sidedef[0] == side ? line->sidedef[1] : line->sidedef[0];
}

RenderPlane PlaneFromRef(const F3DFloor::planeref& ref)
{
	return RenderPlane::From(ref.model, ref.isceiling ? SectorPlaneFace::Ceiling : SectorPlaneFace::Floor);
}

SectorFlags3D FlagsFrom3DFloor(const F3DFloor* floor3D)
{
	const unsigned int ff = floor3D->flags;
	SectorFlags3D flags = SectorFlags3D::None;

	if (ff & FF_SOLID)
		flags = flags | SectorFlags3D::Solid;
	if (ff & FF_SWIMMABLE)
		flags = flags | SectorFlags3D::Swim;
	if (ff & (FF_ALLSIDES | FF_INVERTSIDES))
		flags = flags | SectorFlags3D::RenderInside;
	if ((ff & FF_SOLID) && (ff & FF_SEETHROUGH))
		flags = flags | SectorFlags3D::SightInvert;
	if ((ff & FF_SOLID) && (ff & FF_SHOOTTHROUGH))
		flags = flags | SectorFlags3D::ShootInvert;
	if (ff & FF_NOSHADE)
		flags = flags | SectorFlags3D::DisableLighting;
	if (ff & FF_DOUBLESHADOW)
		flags = flags | SectorFlags3D::RestrictLighting;
	if (ff & FF_RESET)
		flags = flags | SectorFlags3D::ResetLight;
	if (ff & FF_FOG)
		flags = flags | SectorFlags3D::Fog;
	if (floor3D->bottom.isceiling)
		flags = flags | SectorFlags3D::CeilingModel;
	if (ff & FF_UPPERTEXTURE)
		flags = flags | SectorFlags3D::UseParentUpperTexture;
	if (ff & FF_LOWERTEXTURE)
		flags = flags | SectorFlags3D::UseParentLowerTexture;
	if (ff & FF_ADDITIVETRANS)
		flags = flags | SectorFlags3D::AdditiveTransparency;
	if (ff & FF_FADEWALLS)
		flags = flags | SectorFlags3D::NoViewFade;

	if ((ff & (FF_RENDERSIDES | FF_RENDERPLANES)) == 0)
	{
		flags = flags | SectorFlags3D::NoRender;
		if ((ff & FF_NOSHADE) == 0)
			flags = flags | SectorFlags3D::LightTransfer;
	}

	return flags;
}

SectorPlanes BaseRenderPlanes(const Sector3D* sector3D)
{
	if (sector3D->Floor3D != nullptr && (sector3D->Floor3D->flags & FF_RENDERPLANES) == 0)
		return SectorPlanes::None;
	return SectorPlanes::Floor | SectorPlanes::Ceiling;
}

}

Sector3D::Sector3D(int sectorId, sector_t* parentSector, F3DFloor* floor3D)
{
	ParentSector = parentSector;
	ParentSectorId = parentSector->Index();
	SectorId = sectorId;
	ControlSector = floor3D->model;
	ControlSide = floor3D->master->sidedef[0];
	Floor3D = floor3D;
	LightTop = ParentSector;
	LightBottom = ParentSector;
	Flags = FlagsFrom3DFloor(floor3D);
	LightFlags = SectorLightFlags3D::None;
	RenderSides = (floor3D->flags & FF_RENDERSIDES) != 0;

	Alpha = std::clamp(floor3D->alpha, 0, 255) / 255.0f;
	if (HasFlag(Flags, SectorFlags3D::Fog))
		Alpha = 0;

	HasFlipped = HasFlag(Flags, SectorFlags3D::Swim) || HasFlag(Flags, SectorFlags3D::RenderInside);

	if (HasFlag(Flags, SectorFlags3D::AdditiveTransparency))
		RenderDataStyle = Render::OpenGL::Renderers::Legacy::World::Data::RenderDataStyle::Add;
	else if (Alpha < 1)
		RenderDataStyle = Render::OpenGL::Renderers::Legacy::World::Data::RenderDataStyle::Translucent;
	else
		RenderDataStyle = Render::OpenGL::Renderers::Legacy::World::Data::RenderDataStyle::Normal;

	ClipStyle_ = (RenderDataStyle == Render::OpenGL::Renderers::Legacy::World::Data::RenderDataStyle::Normal && !HasFlag(Flags, SectorFlags3D::LightTransfer)) ? ClipStyle::Solid : ClipStyle::NotSolid;

	RenderPlanes = BaseRenderPlanes(this);
	RefreshPlanes();

	ClipBottomZ = ControlBottom.Z;
	ClipPrevBottomZ = ControlBottom.PrevZ;
}

float Sector3D::GetOffsetX() const
{
	return (float)ControlSide->GetTextureXOffset(side_t::mid);
}

float Sector3D::GetOffsetY() const
{
	return (float)ControlSide->GetTextureYOffset(side_t::mid);
}

void Sector3D::RefreshPlanes()
{
	ControlTop = PlaneFromRef(Floor3D->top);
	ControlBottom = PlaneFromRef(Floor3D->bottom);

	const int idTop = SectorId * 2 + 1;
	const int idBottom = SectorId * 2;

	// Z/PrevZ of the fake planes are set by CalculateHeights.
	FakeTop = ControlTop;
	FakeTop.Id = idTop;
	FakeBottom = ControlBottom;
	FakeBottom.Id = idBottom;

	if (HasFlipped)
	{
		FakeTopFlipped = FakeTop;
		FakeBottomFlipped = FakeBottom;
	}
}

void Sector3D::CalculateHeights()
{
	WallHeights_ = WallHeights(ControlTop.Z, ControlBottom.Z, ControlTop.PrevZ, ControlBottom.PrevZ);
	WallHeightsUnclipped = WallHeights_;
	ClipBottomZ = ControlBottom.Z;
	// Helion assigns ControlBottom.Z here (defect: the clipped bottom would not interpolate while the control sector moves).
	ClipPrevBottomZ = ControlBottom.PrevZ;

	auto& sectors3D = Table->Sectors3D(ParentSector);
	if (sectors3D.empty())
		return;

	for (size_t i = 0; i < sectors3D.size(); i++)
	{
		Sector3D* checkSector3D = sectors3D[i].get();
		if (checkSector3D == this)
		{
			if (i < sectors3D.size() - 1)
			{
				checkSector3D = sectors3D[i + 1].get();
				if (checkSector3D->ControlTop.Z > WallHeights_.BottomZ)
				{
					WallHeights_.SetClipped = true;
					if (ShouldClipSector3D(checkSector3D, true))
					{
						WallHeights_.Clipped = true;
						WallHeights_.BottomZ = checkSector3D->ControlTop.Z;
						WallHeights_.PrevBottomZ = checkSector3D->ControlTop.Z;
						ClipBottomZ = checkSector3D->ControlTop.Z;
						ClipPrevBottomZ = checkSector3D->ControlTop.PrevZ;
					}
				}
			}

			break;
		}

		if (ControlBottom.Z < checkSector3D->ControlTop.Z && ControlTop.Z > checkSector3D->ClipBottomZ && ShouldClipSector3D(checkSector3D, false))
		{
			if (!AdjustWallHeights(WallHeights_,
				checkSector3D->ControlTop.Z, checkSector3D->ClipBottomZ, checkSector3D->ControlTop.PrevZ, checkSector3D->ClipPrevBottomZ))
				break;
		}
	}

	FakeTop.Z = WallHeights_.TopZ;
	FakeTop.PrevZ = WallHeights_.PrevTopZ;
	FakeBottom.Z = WallHeights_.BottomZ;
	FakeBottom.PrevZ = WallHeights_.PrevBottomZ;

	if (HasFlipped)
	{
		FakeTopFlipped.Z = WallHeights_.TopZ;
		FakeTopFlipped.PrevZ = WallHeights_.PrevTopZ;
		// UZDoom: Helion assigns FakeBottom here a second time; FakeBottomFlipped is the intended target.
		FakeBottomFlipped.Z = WallHeights_.BottomZ;
		FakeBottomFlipped.PrevZ = WallHeights_.PrevBottomZ;
	}
}

WallHeights& Sector3D::GetWallHeights()
{
	if (RenderDataStyle == Render::OpenGL::Renderers::Legacy::World::Data::RenderDataStyle::Normal)
		return WallHeights_;
	return WallHeightsUnclipped;
}

bool Sector3D::CalculateWallHeights(side_t* side, WallHeights& newWallHeights)
{
	newWallHeights = GetWallHeights();
	WallVertices wall;
	side_t* partner = PartnerSide(side);
	const ::Helion::Geometry::Vec2F noUV(0, 0);

	if (partner == nullptr)
	{
		RenderPlane floor = RenderPlane::From(side->sector, SectorPlaneFace::Floor);
		RenderPlane ceiling = RenderPlane::From(side->sector, SectorPlaneFace::Ceiling);
		WorldTriangulator::HandleOneSided(side, side, floor, ceiling, noUV, wall,
			WorldTriangulator::NoOverride, WorldTriangulator::NoOverride, true, false);
		return AdjustWallHeights(newWallHeights, wall.TopLeft.Z, wall.BottomRight.Z, wall.TopLeft.PrevZ, wall.BottomRight.PrevZ);
	}

	RenderSector sideSector = side->sector;
	RenderSector partnerSector = partner->sector;

	if (GeometryRenderer::LowerIsVisible(side, sideSector, partnerSector))
	{
		WorldTriangulator::HandleTwoSidedLower(side, partnerSector.Floor, sideSector.Floor, noUV, true, wall, false);
		if (WallVerticesOccluded(newWallHeights, wall.TopLeft.Z, wall.BottomRight.Z))
			return false;

		if (!AdjustWallHeights(newWallHeights, wall.TopLeft.Z, wall.BottomRight.Z, wall.TopLeft.PrevZ, wall.BottomRight.PrevZ))
			return false;
	}

	if (GeometryRenderer::UpperIsVisible(side, sideSector, partnerSector))
	{
		WorldTriangulator::HandleTwoSidedUpper(side, sideSector.Ceiling, partnerSector.Ceiling, noUV, true, wall,
			WorldTriangulator::NoOverride, false);
		if (WallVerticesOccluded(newWallHeights, wall.TopLeft.Z, wall.BottomRight.Z))
			return false;

		if (!AdjustWallHeights(newWallHeights, wall.TopLeft.Z, wall.BottomRight.Z, wall.TopLeft.PrevZ, wall.BottomRight.PrevZ))
			return false;
	}

	// Only clip if this side has 3D sectors. Otherwise overlapping non-solid sectors won't render.
	if (Table->HasSectors3D(partner->sector))
	{
		if (!AdjustWallHeights3D(side->sector, newWallHeights, false))
			return false;
	}

	if (Table->HasSectors3D(side->sector))
	{
		if (!AdjustWallHeights3D(partner->sector, newWallHeights, false))
			return false;
	}

	return true;
}

bool Sector3D::WallVerticesOccluded(WallHeights& newWallHeights, double topLeftZ, double bottomRightZ)
{
	newWallHeights.Invalid = topLeftZ >= newWallHeights.TopZ && bottomRightZ <= newWallHeights.BottomZ;
	return newWallHeights.Invalid;
}

bool Sector3D::AdjustWallHeights3D(sector_t* sector, WallHeights& newWallHeights, bool clipOtherSolid)
{
	// UZDoom: the 3D floors of the sector on the other side of the line clip this wall like HWWall::ClipFFloors does:
	// only a swimmable or translucent wall is clipped, and only by a neighbour floor that renders its sides and has the
	// same swimmable/translucent kind. Helion's own rule (ShouldClipSector3D) clips any two non-solid floors, which cut
	// the water side of a neighbour away where it adjoins a plain non-solid floor (3dfloortest, tag 112/113).
	const bool neighbour = sector != ParentSector;
	const unsigned int clipKind = Floor3D->flags & (FF_SWIMMABLE | FF_TRANSLUCENT);
	if (neighbour && clipKind == 0)
		return true;

	auto& sectors3D = Table->Sectors3D(sector);
	for (size_t i = 0; i < sectors3D.size(); i++)
	{
		Sector3D* sector3D = sectors3D[i].get();
		if (neighbour)
		{
			if (!(sector3D->Floor3D->flags & FF_RENDERSIDES) || (sector3D->Floor3D->flags & (FF_SWIMMABLE | FF_TRANSLUCENT)) != clipKind)
				continue;
		}
		else if (!ShouldClipSector3D(sector3D, true, clipOtherSolid))
			continue;

		if (!AdjustWallHeights(newWallHeights, sector3D->ControlTop.Z, sector3D->ControlBottom.PrevZ, sector3D->ControlTop.Z, sector3D->ControlBottom.PrevZ))
			return false;
	}

	return true;
}

bool Sector3D::ShouldClipSector3D(const Sector3D* other, bool clipSolid, bool clipOtherSolid) const
{
	if (other == this)
		return false;

	if (RenderDataStyle != Render::OpenGL::Renderers::Legacy::World::Data::RenderDataStyle::Normal &&
		other->RenderDataStyle != Render::OpenGL::Renderers::Legacy::World::Data::RenderDataStyle::Normal)
	{
		if ((Alpha == 0 && other->Alpha != 0) || (Alpha != 0 && other->Alpha == 0))
			return false;

		return true;
	}

	if (ClipStyle_ != other->ClipStyle_ && (IsLightTransfer() || other->IsLightTransfer()))
		return false;

	if (clipOtherSolid && ClipStyle_ == ClipStyle::NotSolid && other->ClipStyle_ == ClipStyle::Solid)
		return true;

	const int currentSolid = (int)(Flags & SectorFlags3D::Solid);
	const int otherSolid = (int)(other->Flags & SectorFlags3D::Solid);
	if (clipSolid && currentSolid != 0 && otherSolid != 0)
		return false;

	return currentSolid == otherSolid;
}

bool Sector3D::AdjustWallHeights(WallHeights& wallHeights,
	double checkTopZ, double checkBottomZ, double checkPrevTopZ, double checkPrevBottomZ)
{
	if (checkTopZ < wallHeights.TopZ && checkTopZ >= wallHeights.BottomZ)
	{
		wallHeights.BottomZ = checkTopZ;
		wallHeights.PrevBottomZ = checkPrevTopZ;
	}

	if (checkBottomZ > wallHeights.BottomZ && checkBottomZ <= wallHeights.TopZ)
	{
		wallHeights.TopZ = checkBottomZ;
		wallHeights.PrevTopZ = checkPrevBottomZ;
	}

	if (checkTopZ >= wallHeights.TopZ && checkBottomZ <= wallHeights.BottomZ)
	{
		wallHeights.Invalid = true;
		wallHeights.BottomZ = InvalidZ;
		wallHeights.TopZ = InvalidZ;
		wallHeights.PrevTopZ = InvalidZ;
		wallHeights.PrevBottomZ = InvalidZ;
		return false;
	}

	return true;
}

int Sector3D::GetLogicalWallLocation() const
{
	if (HasFlag(Flags, SectorFlags3D::UseParentUpperTexture))
		return side_t::top;
	if (HasFlag(Flags, SectorFlags3D::UseParentLowerTexture))
		return side_t::bottom;
	return side_t::mid;
}

int Sector3D::HeightCompare(const Sector3D* x, const Sector3D* y)
{
	if (y->ControlTop.Z == x->ControlTop.Z)
	{
		const int xId = x->ControlSector->Index();
		const int yId = y->ControlSector->Index();
		return xId < yId ? -1 : (xId > yId ? 1 : 0);
	}

	return y->ControlTop.Z < x->ControlTop.Z ? -1 : 1;
}

// ---------------------------------------------------------------------------------------------------------------
// Sector3DTable
// ---------------------------------------------------------------------------------------------------------------

void Sector3DTable::UpdateTo(FLevelLocals* level)
{
	m_level = level;
	m_sectors.clear();
	m_sectors.resize(level->sectors.Size());

	int nextSectorId = (int)level->sectors.Size();
	for (unsigned i = 0; i < level->sectors.Size(); i++)
	{
		sector_t* sector = &level->sectors[i];
		PerSector& data = m_sectors[i];
		data.TransferFloorLight = sector;
		data.TransferCeilingLight = sector;
		if (sector->e == nullptr)
			continue;

		for (unsigned j = 0; j < sector->e->XFloor.ffloors.Size(); j++)
		{
			F3DFloor* floor3D = sector->e->XFloor.ffloors[j];
			if ((floor3D->flags & (FF_EXISTS | FF_THISINSIDE)) != FF_EXISTS)
				continue;

			data.Sectors3D.push_back(std::make_unique<Sector3D>(nextSectorId++, sector, floor3D));
			data.Sectors3D.back()->Table = this;
		}
	}

	UpdateAll();
}

void Sector3DTable::UpdateAll()
{
	for (unsigned i = 0; i < m_sectors.size(); i++)
	{
		if (!m_sectors[i].Sectors3D.empty())
			SetHeights3D(&m_level->sectors[i], SetHeightsMode::Update);
	}
}

bool Sector3DTable::HasSectors3D(const sector_t* sector) const
{
	const int index = sector->Index();
	return index >= 0 && index < (int)m_sectors.size() && !m_sectors[index].Sectors3D.empty();
}

std::vector<std::unique_ptr<Sector3D>>& Sector3DTable::Sectors3D(const sector_t* sector)
{
	const int index = sector->Index();
	if (index < 0 || index >= (int)m_sectors.size())
		return m_emptySectors3D;
	return m_sectors[index].Sectors3D;
}

std::vector<SectorPlane3D>& Sector3DTable::SectorPlanes3D(const sector_t* sector)
{
	const int index = sector->Index();
	if (index < 0 || index >= (int)m_sectors.size())
		return m_emptyPlanes3D;
	return m_sectors[index].SectorPlanes3D;
}

sector_t* Sector3DTable::TransferFloorLightSector(sector_t* sector) const
{
	const int index = sector->Index();
	if (index < 0 || index >= (int)m_sectors.size() || m_sectors[index].TransferFloorLight == nullptr)
		return sector;
	return m_sectors[index].TransferFloorLight;
}

sector_t* Sector3DTable::TransferCeilingLightSector(sector_t* sector) const
{
	const int index = sector->Index();
	if (index < 0 || index >= (int)m_sectors.size() || m_sectors[index].TransferCeilingLight == nullptr)
		return sector;
	return m_sectors[index].TransferCeilingLight;
}

void Sector3DTable::SetHeights3D(sector_t* sector, SetHeightsMode mode)
{
	const int sectorIndex = sector->Index();
	if (sectorIndex < 0 || sectorIndex >= (int)m_sectors.size())
		return;

	PerSector& data = m_sectors[sectorIndex];
	if (data.Sectors3D.empty())
		return;

	SetupPlanesAndSort(sector, data);

	std::vector<SectorPlane3D>& planes = data.SectorPlanes3D;
	sector_t* currentLightSector = sector;
	Sector3D* currentLightSector3D = nullptr;
	sector_t* resetLightSector = sector;
	Sector3D* resetLightSector3D = nullptr;
	bool resetLight = false;
	double resetZ = DBL_MAX;
	SectorPlane3D* lastPlane3D = &planes[0];
	SectorPlane3D* overlapLightPlane3D = &planes[0];

	for (size_t i = 0; i < planes.size(); i++)
	{
		SectorPlane3D& plane3D = planes[i];
		plane3D.NoRenderWall = false;
		Sector3D* sector3D = plane3D.Sector3DPtr;

		// If this plane is the same height as the last for transfer light then don't reset here.
		if (resetLight && resetZ != plane3D.GetZ())
		{
			resetZ = DBL_MAX;
			resetLight = false;
			currentLightSector3D = resetLightSector3D;
			currentLightSector = resetLightSector;
		}

		if (sector3D == nullptr)
		{
			plane3D.LightSector = currentLightSector;

			if (plane3D.Face == PlaneFace3D::Bottom)
				data.TransferFloorLight = currentLightSector;
			else
				data.TransferCeilingLight = currentLightSector;

			if (i > 0 && lastPlane3D->Sector3DPtr != nullptr && lastPlane3D->ControlPlane.Z == plane3D.Plane.Z && lastPlane3D->Face == plane3D.Face)
			{
				SectorPlanes keepPlane = (SectorPlanes)((int)lastPlane3D->Face + 1);
				lastPlane3D->Sector3DPtr->RenderPlanes = lastPlane3D->Sector3DPtr->RenderPlanes & keepPlane;
			}

			continue;
		}

		bool overlapLight = false;
		if (i > 0 && lastPlane3D->Sector3DPtr != nullptr && lastPlane3D->ControlPlane.Z == plane3D.Plane.Z)
		{
			if (lastPlane3D->Face == plane3D.Face && !sector3D->ShouldRenderInsideWalls())
			{
				// Flag previous plane not to render since this one takes precedence
				SectorPlanes keepPlane = (SectorPlanes)((int)lastPlane3D->Face + 1);
				lastPlane3D->Sector3DPtr->RenderPlanes = lastPlane3D->Sector3DPtr->RenderPlanes & keepPlane;
				lastPlane3D->NoRenderWall = true;
			}
			else if (!HasFlag(lastPlane3D->Sector3DPtr->Flags, SectorFlags3D::NoRender) && lastPlane3D->Sector3DPtr != plane3D.Sector3DPtr &&
				lastPlane3D->Sector3DPtr->RenderDataStyle == Render::OpenGL::Renderers::Legacy::World::Data::RenderDataStyle::Normal)
			{
				// Flag to update lighting for the previous 3D sector
				overlapLight = true;
				overlapLightPlane3D = lastPlane3D;
			}
		}

		// If solid bottom plane was clipped then it's not visible
		// TODO should be able to determine for solid top plane
		if (i > 0 && lastPlane3D->Sector3DPtr != nullptr && lastPlane3D->Face == PlaneFace3D::Bottom &&
			lastPlane3D->Sector3DPtr->IsSolid() && lastPlane3D->Sector3DPtr->ClipBottomZ != lastPlane3D->ControlPlane.Z)
			lastPlane3D->Sector3DPtr->RenderPlanes = lastPlane3D->Sector3DPtr->RenderPlanes & SectorPlanes::Ceiling;

		lastPlane3D = &plane3D;
		SetLight(sector3D, plane3D, currentLightSector);

		if (resetLight)
		{
			resetZ = DBL_MAX;
			resetLight = false;
			currentLightSector3D = resetLightSector3D;
			currentLightSector = resetLightSector;
		}

		if (HasFlag(sector3D->Flags, SectorFlags3D::ResetLight))
		{
			resetLightSector = sector;
			resetLightSector3D = nullptr;
		}
		else if (ShouldResetLightSector(plane3D, sector3D))
		{
			resetLightSector = sector3D->ControlSector;
			resetLightSector3D = sector3D;
		}

		if (ShouldCarryLight(currentLightSector3D, sector3D, plane3D, true, resetLight, resetZ))
			continue;

		currentLightSector = sector3D->ControlSector;
		currentLightSector3D = sector3D;

		if (overlapLight && overlapLightPlane3D->Sector3DPtr != nullptr)
			SetLight(overlapLightPlane3D->Sector3DPtr, *overlapLightPlane3D, currentLightSector);
	}
}

void Sector3DTable::SetupPlanesAndSort(sector_t* sector, PerSector& data)
{
	auto& sectors3D = data.Sectors3D;
	auto& planes = data.SectorPlanes3D;

	// Snapshots must be current before sorting and clipping (UZDoom planes are not live references).
	for (auto& sector3D : sectors3D)
		sector3D->RefreshPlanes();

	RenderPlane ceiling = RenderPlane::From(sector, SectorPlaneFace::Ceiling);
	RenderPlane floor = RenderPlane::From(sector, SectorPlaneFace::Floor);

	if (planes.empty())
	{
		planes.reserve((sectors3D.size() + 1) * 2);

		for (auto& sector3D : sectors3D)
		{
			planes.emplace_back(sector3D->ControlTop, sector3D->FakeTop, sector3D.get(), PlaneFace3D::Top, sector3D->ControlSector);
			planes.emplace_back(sector3D->ControlBottom, sector3D->FakeBottom, sector3D.get(), PlaneFace3D::Bottom, sector3D->ControlSector);
		}

		planes.emplace_back(ceiling, ceiling, nullptr, PlaneFace3D::Top, sector);
		planes.emplace_back(floor, floor, nullptr, PlaneFace3D::Bottom, sector);
	}

	std::sort(sectors3D.begin(), sectors3D.end(),
		[](const std::unique_ptr<Sector3D>& a, const std::unique_ptr<Sector3D>& b) { return Sector3D::HeightCompare(a.get(), b.get()) < 0; });

	for (auto& sector3D : sectors3D)
	{
		sector3D->CalculateHeights();
		sector3D->RenderPlanes = BaseRenderPlanes(sector3D.get());
	}

	// The planes keep their CheckCount while their snapshots follow the (clipped) 3D sector planes.
	for (SectorPlane3D& plane3D : planes)
	{
		if (plane3D.Sector3DPtr == nullptr)
		{
			const RenderPlane& real = plane3D.Face == PlaneFace3D::Top ? ceiling : floor;
			plane3D.ControlPlane = real;
			plane3D.Plane = real;
		}
		else if (plane3D.Face == PlaneFace3D::Top)
		{
			plane3D.ControlPlane = plane3D.Sector3DPtr->ControlTop;
			plane3D.Plane = plane3D.Sector3DPtr->FakeTop;
		}
		else
		{
			plane3D.ControlPlane = plane3D.Sector3DPtr->ControlBottom;
			plane3D.Plane = plane3D.Sector3DPtr->FakeBottom;
		}

		plane3D.UpdateSortKey();
	}

	std::sort(planes.begin(), planes.end(),
		[](const SectorPlane3D& a, const SectorPlane3D& b) { return PlaneHeightKeyCompare(a, b) < 0; });
}

bool Sector3DTable::ShouldResetLightSector(const SectorPlane3D& plane3D, const Sector3D* sector3D)
{
	if (!sector3D->IsLightTransfer())
		return !HasFlag(sector3D->Flags, SectorFlags3D::RestrictLighting);

	const double planeZ = plane3D.GetZ();
	return planeZ < sector3D->ControlTop.Z && planeZ > sector3D->ControlBottom.Z;
}

int Sector3DTable::PlaneHeightKeyCompare(const SectorPlane3D& x, const SectorPlane3D& y)
{
	return x.SortKey.CompareTo(y.SortKey);
}

void Sector3DTable::SetLight(Sector3D* sector3D, SectorPlane3D& plane3D, sector_t* lightSector)
{
	plane3D.LightSector = lightSector;

	if (!HasFlag(sector3D->Flags, SectorFlags3D::RestrictLighting))
		plane3D.LightInsideSector = lightSector;
	else
		plane3D.LightInsideSector = sector3D->ControlSector;

	if (plane3D.Face == PlaneFace3D::Bottom)
		sector3D->LightBottom = lightSector;
	else
		sector3D->LightTop = lightSector;
}

bool Sector3DTable::ShouldCarryLight(const Sector3D* currentLightSector3D, const Sector3D* nextSector3D, const SectorPlane3D& nextPlane3D, bool checkLightTransfer, bool& resetLight, double& resetZ)
{
	resetZ = DBL_MAX;
	resetLight = false;

	if (HasFlag(nextSector3D->Flags, SectorFlags3D::RestrictLighting))
	{
		resetLight = true;
		return true;
	}

	if (HasFlag(nextSector3D->Flags, SectorFlags3D::DisableLighting))
		return true;

	if (currentLightSector3D == nullptr)
		return false;

	if (checkLightTransfer && currentLightSector3D->LightFlags != SectorLightFlags3D::None)
	{
		switch (currentLightSector3D->LightFlags)
		{
		// These don't appear to work how they are documented. Both to next types need to allow another to render.
		case SectorLightFlags3D::ToNextTypeZero:
		case SectorLightFlags3D::ToNextAny:
			if (nextSector3D != currentLightSector3D && nextSector3D->LightFlags != SectorLightFlags3D::None)
				return false;
			break;
		case SectorLightFlags3D::ToControlFloor:
			if (nextSector3D == currentLightSector3D && nextPlane3D.Face == PlaneFace3D::Bottom)
			{
				resetZ = nextPlane3D.GetZ();
				resetLight = true;
			}
			break;
		default:
			break;
		}

		return true;
	}

	const SectorFlags3D nextFlags = nextSector3D->Flags;
	const SectorFlags3D currentFlags = currentLightSector3D->Flags;

	if (HasFlag(currentFlags, SectorFlags3D::Swim))
		return !HasFlag(nextFlags, SectorFlags3D::Swim);

	if (HasFlag(currentFlags, SectorFlags3D::Swim) != HasFlag(nextFlags, SectorFlags3D::Swim))
		return false;

	if (HasFlag(currentFlags, SectorFlags3D::LightTransfer) || HasFlag(nextFlags, SectorFlags3D::LightTransfer))
		return false;

	if (!currentLightSector3D->IsSolid() && currentLightSector3D->IsLightTransfer() && nextSector3D->IsSolid())
		return true;

	if (currentLightSector3D->IsSolid() && !nextSector3D->IsSolid())
		return true;

	return false;
}

}
