/*
** Sector3D.h
**
** Port of Helion.World.Geometry.Sectors.Sector3D as an adapter over UZDoom 3D floors (F3DFloor).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion creates a Sector3D per 3D floor with a fake sector, fake lines and fake sides. UZDoom has F3DFloor
** (sector_t::e->XFloor.ffloors) and no fake geometry, so:
**  - ControlSector = F3DFloor::model, ControlSide = F3DFloor::master->sidedef[0], ParentSector = F3DFloor::target
**  - ControlTop/ControlBottom/FakeTop/FakeBottom are RenderPlane snapshots refreshed by Sector3DTable::Update
**  - FakeSector lines do not exist: GeometryRenderer::RenderSectorLine3D walks ParentSector->Lines and derives
**    which parent side faces the fake front/back (see GeometryRenderer.Sector3D.cpp)
**  - flags map from FF_* (see Sector3D.cpp); alpha = F3DFloor::alpha / 255.
** Sector3DTable owns every Sector3D, built from the level at GeometryRenderer::UpdateTo.
*/

#pragma once

#include <memory>
#include <vector>
#include "World/Geometry/Sectors/RenderSector.h"
#include "World/Geometry/Sectors/SectorPlane3D.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderDataStyle.h"

struct sector_t;
struct side_t;
struct F3DFloor;
struct FLevelLocals;

namespace Helion::World::Geometry::Sectors
{

class Sector3DTable;

enum class SectorFlags3D : int
{
	None = 0,
	Solid = 1,
	Swim = 2,
	RenderInside = 8,
	SightInvert = 16,
	ShootInvert = 32,

	DisableLighting = 128,
	// Also forces this sector to the reset light sector.
	RestrictLighting = 256,
	Fog = 512,
	CeilingModel = 1024,
	UseParentUpperTexture = 2048,
	UseParentLowerTexture = 4096,
	AdditiveTransparency = 8192,
	// When this flag is not set UZDoom will render the fog color as a global blend color and not have fade density.
	NoViewFade = 65536,
	// Resets the light sector to the parent sector. Will cause any sector with RestrictLighting to use this reset sector for light properties.
	ResetLight = 131072,

	// Helion Flags
	NoRender = 1 << 18,
	LightTransfer = 1 << 19
};

constexpr SectorFlags3D operator|(SectorFlags3D a, SectorFlags3D b) { return (SectorFlags3D)((int)a | (int)b); }
constexpr SectorFlags3D operator&(SectorFlags3D a, SectorFlags3D b) { return (SectorFlags3D)((int)a & (int)b); }
constexpr bool HasFlag(SectorFlags3D value, SectorFlags3D flag) { return ((int)value & (int)flag) != 0; }

enum class SectorLightFlags3D
{
	None,
	ToNextTypeZero,	// Extra light extends from ceiling of control sector down to top of another type 0 light
	ToControlFloor,	// Extra light extends from ceiling down to the floor of the control sector.
	ToNextAny		// Extra light extends from control sector's ceiling down to the top of another extra light.
};

enum class ClipStyle
{
	Solid,
	NotSolid
};

enum class SetHeightsMode
{
	Init,
	Update,
	MapReload
};

struct WallHeights
{
	double TopZ = 0;
	double BottomZ = 0;
	double PrevTopZ = 0;
	double PrevBottomZ = 0;
	bool Invalid = false;
	bool Clipped = false;
	bool SetClipped = false;

	WallHeights() = default;
	WallHeights(double topZ, double bottomZ, double prevTopZ, double prevBottomZ) :
		TopZ(topZ), BottomZ(bottomZ), PrevTopZ(prevTopZ), PrevBottomZ(prevBottomZ)
	{
	}
};

class Sector3D
{
public:
	static constexpr double InvalidZ = -32768;	// short.MinValue

	int ParentSectorId = 0;
	int SectorId = 0;	// unique id of this 3D sector (render state key); >= level sector count
	int CheckCount = 0;
	sector_t* ParentSector = nullptr;
	sector_t* ControlSector = nullptr;
	RenderPlane ControlTop;
	RenderPlane ControlBottom;
	RenderPlane FakeTop;
	RenderPlane FakeBottom;
	bool HasFlipped = false;	// Helion FakeSectorFlipped != null
	RenderPlane FakeTopFlipped;
	RenderPlane FakeBottomFlipped;
	sector_t* LightTop = nullptr;
	sector_t* LightBottom = nullptr;
	SectorFlags3D Flags = SectorFlags3D::None;
	SectorLightFlags3D LightFlags = SectorLightFlags3D::None;
	float Alpha = 1;
	double ClipBottomZ = 0;
	double ClipPrevBottomZ = 0;
	WallHeights WallHeights_;
	WallHeights WallHeightsUnclipped;
	SectorPlanes RenderPlanes = SectorPlanes::Floor | SectorPlanes::Ceiling;
	// UZDoom: F3DFloor FF_RENDERSIDES. Helion has a single NoRender flag; UZDoom 3D floors can render only planes.
	bool RenderSides = true;
	double LastSlopeTop = 0;
	double LastSlopeBottom = 0;
	int LastSlopeCheckCount = 0;
	side_t* ControlSide = nullptr;
	F3DFloor* Floor3D = nullptr;
	Sector3DTable* Table = nullptr;	// owning table (set by Sector3DTable::UpdateTo), used to reach the sibling 3D sectors
	Render::OpenGL::Renderers::Legacy::World::Data::RenderDataStyle RenderDataStyle = Render::OpenGL::Renderers::Legacy::World::Data::RenderDataStyle::Normal;
	ClipStyle ClipStyle_ = ClipStyle::Solid;

	Sector3D(int sectorId, sector_t* parentSector, F3DFloor* floor3D);

	bool IsSolid() const { return HasFlag(Flags, SectorFlags3D::Solid); }
	bool IsSwimmable() const { return HasFlag(Flags, SectorFlags3D::Swim); }
	bool IsLightTransfer() const { return HasFlag(Flags, SectorFlags3D::LightTransfer); }
	bool ShouldRenderWalls() const { return RenderSides && ControlTop.Z - ControlBottom.Z > 0 && !HasFlag(Flags, SectorFlags3D::NoRender); }
	bool ShouldRenderFlats() const { return ControlTop.Z - ControlBottom.Z >= 0 && !HasFlag(Flags, SectorFlags3D::NoRender); }
	bool ShouldRenderInsideWalls() const { return HasFlag(Flags, SectorFlags3D::RenderInside); }
	bool IsOpaque() const { return RenderDataStyle == Render::OpenGL::Renderers::Legacy::World::Data::RenderDataStyle::Normal; }

	float GetOffsetX() const;
	float GetOffsetY() const;

	// Refreshes ControlTop/ControlBottom/FakeTop/FakeBottom (and flipped) snapshots from the live F3DFloor planes.
	// Fake plane Z is then set by CalculateHeights (called from Sector3DTable::SetHeights3D).
	void RefreshPlanes();

	// Helion Sector3D.GetWallHeights
	WallHeights& GetWallHeights();

	// Helion Sector3D.CalculateWallHeights: side is a real side of the parent sector (or its neighbour).
	bool CalculateWallHeights(side_t* side, WallHeights& newWallHeights);

	void CalculateHeights();

	// Helion Sector3D.GetLogicalWallLocation: returns side_t::top/mid/bottom.
	int GetLogicalWallLocation() const;

	static int HeightCompare(const Sector3D* x, const Sector3D* y);

private:
	bool AdjustWallHeights3D(sector_t* sector, WallHeights& newWallHeights, bool clipOtherSolid);
	bool ShouldClipSector3D(const Sector3D* other, bool clipSolid = false, bool clipOtherSolid = true) const;
	static bool AdjustWallHeights(WallHeights& wallHeights, double checkTopZ, double checkBottomZ, double checkPrevTopZ, double checkPrevBottomZ);
	static bool WallVerticesOccluded(WallHeights& newWallHeights, double topLeftZ, double bottomRightZ);
};

// Per level list of 3D sectors and the sorted plane lists of every sector with 3D floors
// (Helion Sector.Sectors3D / Sector.SectorPlanes3D).
class Sector3DTable
{
public:
	// Builds the table from sector_t::e->XFloor.ffloors of every sector. Returns the number of 3D sectors.
	void UpdateTo(FLevelLocals* level);

	// Recalculates plane snapshots, wall heights, sorting and light assignment of one sector with 3D floors
	// (Helion Sector3D.SetHeights3D(sector, Update)).
	void SetHeights3D(sector_t* sector, SetHeightsMode mode);
	// Calls SetHeights3D for every sector that has 3D floors.
	void UpdateAll();

	bool HasSectors3D(const sector_t* sector) const;
	std::vector<std::unique_ptr<Sector3D>>& Sectors3D(const sector_t* sector);
	std::vector<SectorPlane3D>& SectorPlanes3D(const sector_t* sector);
	// Light sector override of Helion Sector.TransferFloorLightSector / TransferCeilingLightSector after SetHeights3D.
	sector_t* TransferFloorLightSector(sector_t* sector) const;
	sector_t* TransferCeilingLightSector(sector_t* sector) const;

	static int PlaneHeightKeyCompare(const SectorPlane3D& x, const SectorPlane3D& y);

private:
	struct PerSector
	{
		std::vector<std::unique_ptr<Sector3D>> Sectors3D;
		std::vector<SectorPlane3D> SectorPlanes3D;
		sector_t* TransferFloorLight = nullptr;
		sector_t* TransferCeilingLight = nullptr;
	};

	std::vector<PerSector> m_sectors;
	FLevelLocals* m_level = nullptr;
	std::vector<std::unique_ptr<Sector3D>> m_emptySectors3D;
	std::vector<SectorPlane3D> m_emptyPlanes3D;

	void SetupPlanesAndSort(sector_t* sector, PerSector& data);
	static void SetLight(Sector3D* sector3D, SectorPlane3D& plane3D, sector_t* lightSector);
	static bool ShouldResetLightSector(const SectorPlane3D& plane3D, const Sector3D* sector3D);
	static bool ShouldCarryLight(const Sector3D* currentLightSector3D, const Sector3D* nextSector3D, const SectorPlane3D& nextPlane3D, bool checkLightTransfer, bool& resetLight, double& resetZ);
};

}
