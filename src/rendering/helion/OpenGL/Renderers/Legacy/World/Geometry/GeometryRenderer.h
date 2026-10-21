/*
** GeometryRenderer.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.GeometryRenderer (+ GeometryRenderer.Sector3D partial).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** C++ mapping (see the plan's Type and idiom mapping, plus these renderer specific rules):
**  Side                     -> side_t*
**  Wall                     -> WallLocation (Upper/Middle/Lower/Middle3D) on a side_t*; tier = side_t::top/mid/bottom
**  Sector (real or fake)    -> const RenderSector& / RenderSector (World/Geometry/Sectors/RenderSector.h, implicitly built from
**                              sector_t*). Helion mutates shared fake sectors (heights of 3D slices); here the slice sector is a
**                              by-value RenderSector member and real sectors are snapshots, so nothing in sector_t is ever written.
**  SectorPlane              -> const RenderPlane& (derives SectorPlane, adds texture/light/NoRender of the plane)
**  Subsector arrays/sector  -> TArray<FSection*> per sector (flats are built per FSection, plan D29)
**  DynamicVertex[]?         -> DynamicVertexArray* (std::vector, owned by the lookup tables); SkyGeometryVertex[]? -> SkyVertexArray*
**  Span<DynamicVertex>      -> std::span<DynamicVertex>
**  IWorld                   -> FLevelLocals*
**  Func<RenderWallSliceArgs, RenderWallSliceResult> -> std::function<RenderWallSliceResult(const RenderWallSliceArgs&)>
**  Sector3D / SectorPlane3D -> World/Geometry/Sectors/Sector3D.h adapter over F3DFloor (Sector3DTable m_sector3DTable owned here)
**  UZDoom-only methods are marked [UZDoom].
*/

#pragma once

#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>
#include "tarray.h"
#include "Common/Shared/RenderInfo.h"
#include "Common/Shared/World/MidTexOpening.h"
#include "Common/Shared/World/TriangulatedWorldVertex.h"
#include "Common/Shared/World/ViewClipping/ViewClipper.h"
#include "Common/Shared/World/WallVertices.h"
#include "OpenGL/Renderers/Legacy/World/DynamicVertex.h"
#include "OpenGL/Renderers/Legacy/World/StaticVertex.h"
#include "OpenGL/Renderers/Legacy/World/Sky/LegacySkyRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Sky/Sphere/SkyGeometryVertex.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderDataStyle.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderWorldDataManager.h"
// These open the World::Geometry namespace: after every header that uses an unqualified Geometry:: inside Legacy::World.
#include "OpenGL/Renderers/Legacy/World/Geometry/CoverWallUtil.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderMode.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryType.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/RenderWallSliceArgs.h"
#include "OpenGL/Shader/RenderProgram.h"
#include "OpenGL/Texture/Legacy/GLLegacyTexture.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"
#include "Util/Geometry.h"
#include "World/Geometry/Lines/MidTexSpan.h"
#include "World/Geometry/Sectors/RenderSector.h"
#include "World/Geometry/Sectors/Sector3D.h"
#include "World/Geometry/Sectors/SectorPlane3D.h"
#include "World/Geometry/Sectors/SectorPlaneFace.h"
#include "World/Geometry/Sectors/TransferHeightsView.h"
#include "World/Geometry/Walls/WallLocation.h"
#include "World/Static/SideTexture.h"
// Opens the World::Geometry namespace to forward declare GeometryRenderer, so it has to follow every helion header that uses an
// unqualified Geometry:: inside Legacy::World.
#include "OpenGL/Renderers/Legacy/World/Geometry/MidTextureHack.h"

struct side_t;
struct line_t;
struct sector_t;
struct subsector_t;
struct seg_t;
struct FSection;
struct F3DFloor;
struct FLevelLocals;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

namespace Portals { class PortalRenderer; class PortalDriver; }
namespace Static { class StaticCacheGeometryRenderer; }
class DecalRenderer;

class GeometryRenderer
{
public:
	using RenderSector = ::Helion::World::Geometry::Sectors::RenderSector;
	using RenderPlane = ::Helion::World::Geometry::Sectors::RenderPlane;
	using SectorPlanes = ::Helion::World::Geometry::Sectors::SectorPlanes;
	using SectorPlaneFace = ::Helion::World::Geometry::Sectors::SectorPlaneFace;
	using TransferHeightView = ::Helion::World::Geometry::Sectors::TransferHeightView;
	using Sector3D = ::Helion::World::Geometry::Sectors::Sector3D;
	using SectorPlane3D = ::Helion::World::Geometry::Sectors::SectorPlane3D;
	using WallLocation = ::Helion::World::Geometry::Walls::WallLocation;
	using SideTexture = ::Helion::World::Static::SideTexture;
	using MidTexOpening = ::Helion::Render::Common::Shared::World::MidTexOpening;
	using MidTexSpan = ::Helion::World::Geometry::Lines::MidTexSpan;
	using WallVertices = ::Helion::Render::OpenGL::Shared::World::WallVertices;
	using TriangulatedWorldVertex = ::Helion::Render::OpenGL::Shared::World::TriangulatedWorldVertex;
	using GLLegacyTexture = Texture::Legacy::GLLegacyTexture;
	using SkyGeometryVertex = Sky::Sphere::SkyGeometryVertex;
	using RenderCoverWallAction = std::function<void(side_t*, std::span<DynamicVertex>, WallLocation, bool)>;
	using RenderSliceFunc = std::function<RenderWallSliceResult(const RenderWallSliceArgs&)>;
	// Helion Action<Side, Wall, Sector, GLLegacyTexture?, Span<DynamicVertex>, Sector3D?>
	using RenderVertices3DAction = std::function<void(side_t*, WallLocation, const RenderSector&, GLLegacyTexture*, std::span<DynamicVertex>, Sector3D*)>;

	// Helion: GeometryRenderer(IConfig, ArchiveCollection, LegacyGLTextureManager, RenderProgram, RenderProgram staticProgram,
	// RenderWorldDataManager, ViewClipper, ViewClipper, RenderProfiler, bool unitTest). Config comes from CVars (HelionCVars.h),
	// the archive collection from TexMan, the profiler is not ported.
	GeometryRenderer(Texture::Legacy::LegacyGLTextureManager* glTextureManager, ::Helion::Render::OpenGL::Shader::RenderProgram* program, ::Helion::Render::OpenGL::Shader::RenderProgram* staticProgram,
		Data::RenderWorldDataManager* worldDataManager, Shared::World::ViewClipping::ViewClipper* viewClipper,
		Shared::World::ViewClipping::ViewClipper* viewClipperPrev);
	~GeometryRenderer();

	GeometryRenderer(const GeometryRenderer&) = delete;
	GeometryRenderer& operator=(const GeometryRenderer&) = delete;

	Portals::PortalRenderer& Portals() { return *m_portals; }
	Static::StaticCacheGeometryRenderer& StaticRenderer() { return *m_staticCacheGeometryRenderer; }
	// Helion FogSide: dummy side (never a level side) whose RenderDataStyle is FogBarrier; see IsFogSide().
	side_t* FogSide() { return m_fogSide.get(); }
	bool IsFogSide(const side_t* side) const { return side == m_fogSide.get(); }
	::Helion::World::Geometry::Sectors::Sector3DTable& Sector3DTable() { return m_sector3DTable; }
	Sky::LegacySkyRenderer& SkyRenderer() { return *m_skyRenderer; }

	void UpdateTo(FLevelLocals* world, bool sameAsPreviousMap = false);
	void Clear(double tickFraction, bool newTick);
	void ClearBsp();

	void RenderStaticGeometryWalls();
	void RenderStaticGeometryFlats();
	void RenderStaticCoverWalls();
	void RenderStaticOneSidedCoverWalls();
	void RenderStaticTwoSidedWalls();
	void RenderStaticMiddle3D();
	void RenderStaticSkies(Shared::RenderInfo& renderInfo);
	// renderStatic: the frame draws the static cache (its skies). staticSkyVisible: a sky is in view (BspHeuristics::SkyVisible).
	void RenderSkies(Shared::RenderInfo& renderInfo, bool renderStatic, bool staticSkyVisible);
	void RenderPortals(Shared::RenderInfo& renderInfo);
	void RenderWallClipPortals(Shared::RenderInfo& renderInfo);
	void RenderStaticStyle(Data::RenderDataStyle style);

	void RenderSubsector(subsector_t* subsector, const Helion::Geometry::Vec2D& pos2D, const Helion::Geometry::Vec2D& prevPos2D, bool renderSectorFlats);
	// Helion RenderSector(Sector, ...): renamed because C++ cannot have a member function named like the RenderSector type alias.
	void RenderSectorGeometry(sector_t* sector, const Helion::Geometry::Vec3D& viewPosition, const Helion::Geometry::Vec3D& prevViewPosition);
	void RenderSectorWall(sector_t* sector, line_t* line, const Helion::Geometry::Vec3D& viewPosition, const Helion::Geometry::Vec3D& prevViewPosition);
	// UZDoom (plan D27, classic AddPolyobjs): a polyobject side is lit and bounded by the sector it is in now (currentsector
	// of the classic BSP walk), not by the sector of its original sidedef. Its vertices are rebuilt every time.
	void RenderPolyobjectWall(sector_t* containingSector, side_t* side, const Helion::Geometry::Vec3D& viewPosition, const Helion::Geometry::Vec3D& prevViewPosition);
	// UZDoom-only (GeometryRenderer.Decals.cpp): hands the solid wall slices of side to DecalRenderer::RenderDecals.
	void RenderSideDecals(side_t* side, DecalRenderer& decalRenderer, Shared::RenderInfo& renderInfo);
	void SetInitRender();

	void RenderAlphaSide(side_t* side, bool isFrontSide);
	void RenderSide(side_t* side, bool isFrontSide);
	void RenderOneSided(side_t* side, bool isFront, DynamicVertexArray*& vertices, SkyVertexArray*& skyVertices, GLLegacyTexture*& texture,
		const RenderSector* renderSector = nullptr, const RenderSector* lightLevelSector = nullptr, side_t* offsetSide = nullptr,
		bool renderSkySide = true, bool allowAlpha = false, Data::RenderDataStyle style = Data::RenderDataStyle::Normal,
		GeometryType baseType = GeometryType::Wall);

	// Helion Side.NoCache (m_fogSide, m_fakeSide): never read or written in the per-side lookups, the data is kept alive in m_noCacheData
	// until the next call that stores into the same lookup. SetCachedSide always takes ownership.
	DynamicVertexArray* GetCachedSide(std::vector<std::unique_ptr<DynamicVertexArray>>& lookup, const side_t* side);
	void SetCachedSide(std::vector<std::unique_ptr<DynamicVertexArray>>& lookup, const side_t* side, std::unique_ptr<DynamicVertexArray> data);

	void SetRenderOneSided(side_t* side, const RenderSector* renderSector = nullptr);
	void SetRenderTwoSided(side_t* facingSide, side_t* otherSide);
	void SetRenderFloor(const RenderPlane& floor);
	void SetRenderCeiling(const RenderPlane& ceiling);

	void RenderMidTexCoverWalls(side_t* side, const RenderSector& facingSector, const RenderSector& otherSector, std::span<DynamicVertex> midTexVertices,
		SideTexture visibleTextures, const RenderCoverWallAction& render);
	SectorPlanes GetMidTexClipPlanes(side_t* side, const RenderSector& facingSector, const RenderSector& otherSector, MidTexOpening& opening, MidTexOpening& prevOpening);
	bool IsLowerVisibleWithTransferHeights(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool& transferHeights);

	static bool LowerIsVisible(side_t* facingSide, const RenderSector& facingSector, const RenderSector& otherSector);
	static bool UpperIsVisible(side_t* facingSide, const RenderSector& facingSector, const RenderSector& otherSector);
	// Helion passes the TextureManager as first argument; UZDoom uses TexMan, so it is dropped.
	static bool UpperIsVisibleOrFlood(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool& skyHack);
	static bool UpperOrSkySideIsVisible(side_t* facingSide, const RenderSector& facingSector, const RenderSector& otherSector, bool& skyHack);
	bool ShouldRenderFogBarrier(side_t* side, side_t* otherSide);

	// Helion out DynamicVertex[]? vertices / SkyGeometryVertex[]? skyVertices, skyVertices2; lightLevelSector (null = facingSector).
	void RenderTwoSidedLower(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide,
		DynamicVertexArray*& vertices, SkyVertexArray*& skyVertices, SkyVertexArray*& skyVertices2, const RenderSector* lightLevelSector = nullptr,
		bool renderSkySide = true);
	void RenderTwoSidedUpper(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide,
		DynamicVertexArray*& vertices, SkyVertexArray*& skyVertices, SkyVertexArray*& skyVertices2, const RenderSector* lightLevelSector = nullptr,
		bool renderSkySide = true);
	DynamicVertexArray* RenderTwoSidedUpperOrLowerRaw(WallLocation location, side_t* facingSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide);
	void RenderSkySide(side_t* facingSide, const RenderSector& facingSector, SectorPlaneFace face, bool isFront, SkyVertexArray*& skyVertices);
	void RenderFogBarrier(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide,
		DynamicVertexArray*& vertices, const RenderSector* lightLevelSector = nullptr, const MidTexSpan* restrictSpan = nullptr);
	void RenderTwoSidedMiddle(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide,
		DynamicVertexArray*& vertices, const RenderSector* lightLevelSector = nullptr, const MidTexSpan* restrictSpan = nullptr);
	SectorPlanes GetTwoSidedMiddleClipPlanes(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector);

	static double GetTransferHeightHackOffset(side_t* facingSide, side_t* otherSide, double bottomZ, double topZ, bool previous);
	static MidTexOpening GetMidTexOpening(side_t* facingSide, const RenderSector& facingSector, const RenderSector& otherSector, bool previous);
	static MidTexSpan GetMidTexSpan(const Helion::Geometry::Dimension& dimension, side_t* front, side_t* back, const RenderSector& frontSector, const RenderSector& backSector,
		double offset = 0, double prevOffset = 0);
	GeometryType GetWallType(side_t* side, WallLocation wall, Sector3D* sector3D);

	void SetRenderMode(GeometryRenderMode renderMode, TransferHeightView view, bool newTick = false);
	void SetViewPosition(const Helion::Geometry::Vec3D& viewPosition, const Helion::Geometry::Vec3D& prevViewPosition);
	void SetBuffer(bool set) { m_buffer = set; }
	// UZDoom (classic AddLine): an out of bounds view (FRenderViewpoint::bDoOob) does not add the walls to the view clipper.
	void SetOccludeWithWalls(bool set) { m_occludeWithWalls = set; }
	// UZDoom (r_dithertransparency, DitherTransparency.h): the walk marks the flagged side tiers / planes as dithered.
	void SetDither(bool set) { m_dither = set; }
	// UZDoom (r_distance_cull_type, classic IsDistanceCulled): the BSP walk culls the lines whose closest point is farther than the
	// distance from center; cullDistanceSquared <= 0 disables it.
	void SetDistanceCull(const Helion::Geometry::Vec2D& center, double cullDistanceSquared) { m_cullCenter = center; m_cullDistanceSquared = cullDistanceSquared; }
	void SetBufferCoverWall(bool set);
	// UZDoom (plan M5.2): the BSP walk hands its visible segs / subsectors to the portal driver (null = no portal collection).
	void SetPortalDriver(Portals::PortalDriver* driver) { m_portalDriver = driver; }

	// The FSections are those of renderSector.Id (the real sector); lightLevelSector null = the sector as seen from the current transfer
	// heights view.
	void RenderSectorFlats(const RenderSector& renderSector, const RenderPlane& renderPlane, const RenderPlane& geometryPlane, bool floor, bool renderFlood,
		DynamicVertexArray*& vertices, SkyVertexArray*& skyVertices, const RenderSector* lightLevelSector = nullptr, bool allowAlpha = false, float alpha = 1,
		Data::RenderDataStyle style = Data::RenderDataStyle::Normal);
	// UZDoom: there is no black texture index in TexMan; a flat without texture ("-", index 0) that does not allow alpha returns this
	// negative handle, which GetFlatGLTexture() resolves to GLTextureManager::BlackTexture (GetTexture(negative) is the null texture).
	static constexpr int BlackTextureHandle = -2;
	int GetFlatTextureHandle(int textureHandle, bool allowAlpha) const;
	GLLegacyTexture* GetFlatGLTexture(int flatTextureHandle);

	// --- GeometryRenderer.Sector3D.cs
	void SetSectorForLineRendering3D(Sector3D* sector3D);
	void RenderSectorLine3D(Sector3D* sector3D, int lineIndex, bool renderFront, bool renderBack, const RenderVertices3DAction& renderVertices);
	RenderWallSliceResult RenderWallSlices3D(side_t* side, WallLocation wall, bool isFrontSide, side_t* otherSide, const RenderSector& facingSector,
		const RenderSector& otherSector, std::span<SectorPlane3D> traversePlanes3D, const RenderSliceFunc& renderFunc, side_t* offsetSide = nullptr,
		bool renderSkySide = true, bool allowAlpha = false, Sector3D* anchorSector3D = nullptr,
		const ::Helion::World::Geometry::Sectors::WallHeights* wallHeights3D = nullptr, Data::RenderDataStyle style = Data::RenderDataStyle::Normal,
		sector_t* lightSector3D = nullptr);
	bool SetSectorsForSlice3D(const RenderWallSliceArgs& args, RenderSector& facing);
	RenderWallSliceResult RenderSectorSlice3D(const RenderWallSliceArgs& args);
	RenderWallSliceResult RenderOneSidedSlice(const RenderWallSliceArgs& args);
	bool SetSectorsForTwoSidedLowerSlice(const RenderWallSliceArgs& args, RenderSector& facing, RenderSector& other);
	RenderWallSliceResult RenderTwoSidedLowerSlice(const RenderWallSliceArgs& args);
	bool SetSectorsForTwoSidedUpperSlice(const RenderWallSliceArgs& args, RenderSector& facing, RenderSector& other);
	RenderWallSliceResult RenderTwoSidedUpperSlice(const RenderWallSliceArgs& args);
	bool SetSectorsForTwoMiddleSlice(const RenderWallSliceArgs& args, RenderSector& facing, RenderSector& other, double& bottomZ);
	RenderWallSliceResult RenderTwoSidedMiddleSlice(const RenderWallSliceArgs& args);

	const RenderSliceFunc& OneSidedSliceFunc() const { return m_renderOneSidedSliceFunc; }
	const RenderSliceFunc& TwoSidedLowerSliceFunc() const { return m_renderTwoSidedLowerSliceFunc; }
	const RenderSliceFunc& TwoSidedUpperSliceFunc() const { return m_renderTwoSidedUpperSliceFunc; }
	const RenderSliceFunc& TwoSidedMiddleSliceFunc() const { return m_renderTwoSidedMiddleSliceFunc; }
	const RenderSliceFunc& SectorSliceFunc3D() const { return m_renderSectorSliceFunc3D; }

	// [UZDoom] The real sectors of a level that are drawn through GeometryRenderer, for the static cache.
	// The sections (flat groups) of a sector, replaces Helion's per-sector Subsector list.
	TArrayView<FSection*> SectionsOfSector(const sector_t* sector);

	void Dispose();

	// [UZDoom] Called once per frame before rendering (LegacyWorldRenderer::ValidateSignatures partner): refreshes 3D floor snapshots.
	void RefreshSector3D();

	static constexpr double MaxSky = 16384;

	// [UZDoom] Helion Constants.NoTextureIndex / NullCompatibilityTextureIndex. UZDoom has no separate "NULL" compatibility texture:
	// "-" is FTextureID index 0 and a missing texture is index -1, both are caught by Helion's `<= NullCompatibilityTextureIndex` tests.
	static constexpr int NoTextureIndex = 0;
	static constexpr int NullCompatibilityTextureIndex = 0;
	// Helion TextureManager.IsSkyTexture (texture handle == FTextureID::GetIndex()).
	static bool IsSkyTexture(int textureHandle);

	// [UZDoom] The render counters Helion keeps on Side / SectorPlane. sector_t / side_t only carry the dynamic flags, the static
	// cache state (LastRenderChangeGametick) lives in RenderStaticState, the rest is here, indexed by side / plane id.
	struct SideRenderCounters
	{
		int LastRenderGametick = 0;
		int LastRenderGametickAlpha = 0;
		int UpperInvalidatedCount = 0;
		int MiddleInvalidatedCount = 0;
		int LowerInvalidatedCount = 0;
		int AlphaInvalidatedCount = 0;
	};
	struct PlaneRenderCounters
	{
		int LastRenderGametick = 0;
		int InvalidatedCount = 0;
	};
	// Must not be called with m_fakeSide / m_fogSide / m_emptyTraverseSide (they are not level sides).
	SideRenderCounters& SideCounters(const side_t* side);
	// planeId: RenderPlane::Id (sector index * 2 + face for real planes, ids >= 2 * sector count for 3D planes).
	PlaneRenderCounters& PlaneCounters(int planeId);
	// Helion Gametick (RenderStaticState::GameTick).
	static int Gametick();
	// Helion Side.CheckRenderingChanged
	bool CheckSideRenderingChanged(const side_t* side);
	// Helion Sector.CheckRenderingChanged(gametick, checkTransferHeights)
	bool CheckSectorRenderingChanged(const sector_t* sector, int gametick, bool checkTransferHeights = true);
	// Helion SectorPlane.CheckRenderingChanged. 3D planes have no static cache state: only PrevZ != Z counts.
	bool CheckPlaneRenderingChanged(const RenderPlane& plane);

private:
	// Helion Side.OffsetChanged (ScrollData != null) and Side.Lower/Middle/UpperInvalidatedCount (tier Upper/Middle/Lower).
	bool SideOffsetChanged(const side_t* side) const;
	int& SideInvalidatedCount(const side_t* side, WallLocation tier);
	bool CheckPlaneRenderingChanged(const sector_t* sector, int pos);
	void RenderSector3D(Sector3D* sector3D, const Helion::Geometry::Vec3D& viewPosition, const Helion::Geometry::Vec3D& prevViewPosition);
	void RenderSectorFlats3D(Sector3D* sector3D);
	sector_t* RealSectorOfPlane(const RenderPlane& plane);
	std::vector<SideRenderCounters> m_sideCounters;
	std::vector<PlaneRenderCounters> m_planeCounters;	// [sector index * 2 + face]
	std::unordered_map<int, PlaneRenderCounters> m_planeCounters3D;
	std::unordered_map<const void*, std::unique_ptr<DynamicVertexArray>> m_noCacheData;
	SideRenderCounters m_scratchSideCounters;	// counters of the sentinel sides (m_fakeSide, m_fogSide, m_emptyTraverseSide)
	bool m_disposed = false;

	struct LoadTextureCounts
	{
		std::vector<int> FlatTextures;
		std::vector<int> WallTexturesRepeat;
		std::vector<int> WallTexturesClamp;
	};

	Portals::PortalRenderer* m_portals = nullptr;
	::Helion::Render::OpenGL::Shader::RenderProgram* m_program;
	Texture::Legacy::LegacyGLTextureManager* m_glTextureManager;
	Static::StaticCacheGeometryRenderer* m_staticCacheGeometryRenderer = nullptr;
	std::unique_ptr<Portals::PortalRenderer> m_portalsOwner;
	std::unique_ptr<Static::StaticCacheGeometryRenderer> m_staticCacheOwner;
	TArray<TriangulatedWorldVertex> m_subsectorVertices;
	TArray<uint32_t> m_sectionIndices;	// [UZDoom] HandleSection output
	DynamicVertexArray m_wallVertices = DynamicVertexArray(6);
	DynamicVertex m_coverWallVertices[6] = {};
	SkyVertexArray m_skyWallVertices = SkyVertexArray(6);
	// [UZDoom] Sky wall vertices of NoCache sides (m_fogSide/m_fakeSide, Helion Side.NoCache): owned here instead of the per side lookups.
	std::unique_ptr<SkyVertexArray> m_noCacheSkyLower;
	std::unique_ptr<SkyVertexArray> m_noCacheSkyUpper;
	Data::RenderWorldDataManager* m_worldDataManager;
	std::unique_ptr<Sky::LegacySkyRenderer> m_skyRenderer;
	MidTextureHack m_midTextureHack;
	RenderPlane m_fakeFloor;
	RenderPlane m_fakeCeiling;
	RenderCoverWallAction m_renderCoverWallAction;
	double m_tickFraction = 0;
	bool m_floorChanged = false;
	bool m_ceilingChanged = false;
	bool m_sectorChangedLine = false;
	int m_contrastMode = 0;	// r_fakecontrast (RenderContrastMode)
	bool m_vanillaRender = false;
	bool m_renderCoverOnly = false;
	bool m_pixelGapCorrection = false;
	GeometryRenderMode m_renderMode = GeometryRenderMode::Dynamic;
	bool m_buffer = true;
	bool m_occludeWithWalls = true;
	bool m_dither = false;
	Helion::Geometry::Vec2D m_cullCenter;
	double m_cullDistanceSquared = 0;
	DynamicVertexArray m_culledWall;
	Helion::Geometry::Vec3D m_viewPosition;
	Helion::Geometry::Vec3D m_prevViewPosition;
	// The sector a polyobject side is drawn in (RenderPolyobjectWall), null otherwise.
	sector_t* m_polyobjectSector = nullptr;
	FLevelLocals* m_world = nullptr;
	TransferHeightView m_transferHeightsView = TransferHeightView::Middle;
	TransferHeightView m_prevTransferHeightsView = TransferHeightView::Middle;
	std::vector<std::unique_ptr<DynamicVertexArray>> m_vertexLookup;
	std::vector<std::unique_ptr<DynamicVertexArray>> m_vertexLowerLookup;
	std::vector<std::unique_ptr<DynamicVertexArray>> m_vertexUpperLookup;
	std::vector<std::unique_ptr<SkyVertexArray>> m_skyWallVertexLowerLookup;
	std::vector<std::unique_ptr<SkyVertexArray>> m_skyWallVertexUpperLookup;
	std::vector<std::unique_ptr<DynamicVertexArray>> m_vertexFloorLookup;
	std::vector<std::unique_ptr<DynamicVertexArray>> m_vertexCeilingLookup;
	std::vector<std::unique_ptr<SkyVertexArray>> m_skyFloorVertexLookup;
	std::vector<std::unique_ptr<SkyVertexArray>> m_skyCeilingVertexLookup;
	std::vector<int> m_drawnSides;
	std::unordered_map<int, std::unique_ptr<DynamicVertexArray>> m_vertexPlaneLookup3D;
	std::unique_ptr<side_t> m_fogSide;
	std::vector<char> m_hitLines;	// BitArray
	int m_invalidatedCounter = 1;
	// Helion SectorPlane.InvalidatedCount: per plane (RenderPlane::Id) counter checked by RenderFlat.
	std::unordered_map<int, int> m_flatInvalidatedCounts;

	RenderSliceFunc m_renderOneSidedSliceFunc;
	RenderSliceFunc m_renderTwoSidedLowerSliceFunc;
	RenderSliceFunc m_renderTwoSidedUpperSliceFunc;
	RenderSliceFunc m_renderTwoSidedMiddleSliceFunc;

	Shared::World::ViewClipping::ViewClipper* m_viewClipper;
	Shared::World::ViewClipping::ViewClipper* m_viewClipperPrev;
	Portals::PortalDriver* m_portalDriver = nullptr;

	// UZDoom
	TArray<TArray<FSection*>> m_sectionsBySector;
	::Helion::World::Geometry::Sectors::Sector3DTable m_sector3DTable;

	// --- GeometryRenderer.Sector3D.cs state
	RenderSliceFunc m_renderSectorSliceFunc3D;
	std::vector<DynamicVertex> m_vertices;
	std::unique_ptr<side_t> m_fakeSide;
	std::unique_ptr<side_t> m_emptyTraverseSide;
	RenderPlane m_fakeAnchorTopPlane;
	RenderSector m_wallSector;
	RenderSector m_fakeFacing;
	RenderSector m_fakeOther;
	RenderSector m_sliceSector;
	RenderSector m_emptyTraverseSector;
	std::vector<SectorPlane3D> m_mergePlanes;
	std::unique_ptr<MidTexSpan> m_midTexSpan;
	float m_wallScrollY = 0;
	float m_wallScrollPrevY = 0;
	// UZDoom: the planes that bound the one sided wall slice being built (RenderWallSlices3D), null outside a slice.
	const RenderPlane* m_sliceSlopeTop = nullptr;
	const RenderPlane* m_sliceSlopeBottom = nullptr;
	// UZDoom: copies of the slice planes with sloped plane heights taken at the current line (RenderWallSlices3D).
	std::vector<SectorPlane3D> m_slopePlanes;

	// --- GeometryRenderer.cs private
	bool IsLevelSide(const side_t* side) const;
	void UpdateVertexWallLookup(std::vector<std::unique_ptr<DynamicVertexArray>>& vertices, int sideCount, bool free);
	void UpdateSkyWallLookup(std::vector<std::unique_ptr<SkyVertexArray>>& vertices, int sideCount, bool free);
	static void UpdateFlatVertices(std::vector<std::unique_ptr<DynamicVertexArray>>& vertices, int sectorCount, bool free);
	static void UpdateSkyFlatVertices(std::vector<std::unique_ptr<SkyVertexArray>>& vertices, int sectorCount, bool free);
	void SetRenderCompatibility(FLevelLocals* world);
	static void SetFloodSectors(FLevelLocals* world);
	void RenderSubsectorWalls(subsector_t* subsector, const Helion::Geometry::Vec2D& pos2D, const Helion::Geometry::Vec2D& prevPos2D);
	void SetSectorRendering(sector_t* sector);
	void RenderSectorFlats(sector_t* sectorForSubsectors, const RenderSector& renderSector, sector_t* set);
	void ReleaseUnmanagedResources();
	LoadTextureCounts PreloadAllTextures(FLevelLocals* world);
	void AddLineSwitchTextures(LoadTextureCounts& counts);
	static void AddSideTextures(LoadTextureCounts& counts, side_t* side, bool twoSided);
	void RenderSectorWalls(sector_t* sector, const Helion::Geometry::Vec2D& pos2D, const Helion::Geometry::Vec2D& prevPos2D);
	void RenderSectorLine(line_t* line, sector_t* sector, const Helion::Geometry::Vec2D& pos2D, const Helion::Geometry::Vec2D& prevPos2D);
	void CheckFloodFillLine(side_t* front, side_t* back);
	void RenderSectorSideWall(sector_t* sector, side_t* side, bool onFrontSide);
	static GeometryType GetGeometryType(Data::RenderDataStyle style, GeometryType baseType);
	int GetLightLevelAdd(side_t* side);
	void RenderTwoSided(side_t* facingSide, bool isFrontSide);
	SideTexture GetSideVisibility(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector);
	bool TwoSidedLowerFlood(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide);
	bool TwoSidedUpperFloodRenderSkySide(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide);
	void RenderCoverWall(WallLocation location, side_t* facingSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide);
	// UZDoom: fromFloor starts the sky at the facing floor (an untextured one sided wall under a sky ceiling, see RenderOneSided).
	void RenderSkySide(side_t* facingSide, const RenderSector& facingSector, const RenderSector* otherSector, GLLegacyTexture* texture, bool isFront, SkyVertexArray*& skyVertices,
		bool fromFloor = false);
	SectorPlanes GetTwoSidedMiddleClipPlanesVanilla(const RenderSector& facingSector, const RenderSector& otherSector);
	bool RenderFlat(TArrayView<FSection*> sections, const RenderPlane& renderPlane, const RenderPlane& geometryPlane, bool floor, bool renderFlood, bool checkViewPos,
		DynamicVertexArray*& vertices, SkyVertexArray*& skyVertices, GLLegacyTexture*& texture, const RenderSector* lightLevelSector = nullptr, bool allowAlpha = false,
		float alpha = 1, Data::RenderDataStyle style = Data::RenderDataStyle::Normal);
	bool ViewPositionForFlatInvalid(const RenderPlane& renderPlane, bool floor);
	DynamicVertexArray* GetSectorVertices3D(TArrayView<FSection*> sections, const RenderPlane& geometryPlane, bool& generate);
	DynamicVertexArray* GetSectorVertices(TArrayView<FSection*> sections, bool floor, int id, bool& generate);
	SkyVertexArray* GetSkySectorVertices(TArrayView<FSection*> sections, bool floor, int id, bool& generate);
	static DynamicVertexArray* InitSectorVertices(TArrayView<FSection*> sections, int id, std::vector<std::unique_ptr<DynamicVertexArray>>& lookup);
	static SkyVertexArray* InitSkyVertices(TArrayView<FSection*> sections, int id, std::vector<std::unique_ptr<SkyVertexArray>>& lookup);
	bool FlatChanged(const RenderPlane& flat);
	static void SetSkyWallVertices(SkyGeometryVertex* data, const WallVertices& wv);
	static void SetSkyWallVertices(SkyVertexArray& data, const WallVertices& wv);
	static std::unique_ptr<SkyVertexArray> CreateSkyWallVertices(const WallVertices& wv);
	static void CreateSkyFlatVertices(SkyVertexArray& vertices, int startIndex, const TriangulatedWorldVertex& root, const TriangulatedWorldVertex& second, const TriangulatedWorldVertex& third);
	// Helion SetWallVertices/GetWallVertices(in WallVertices wv, int lightLevelAdd, int lightIndex, int overrideLightIndex, byte wallLightLevel, int mapId,
	// WallLocation location, int textureIndex, float alpha = 1, int addAlpha = 1). mapId is line->Index(). The Set overloads write 6 vertices.
	// UZDoom: wallLightLevel is signed (-256..255, see VertexOptions::PackRender) for negative relative side light.
	static void SetWallVertices(DynamicVertex* data, const WallVertices& wv, int lightLevelAdd, int lightIndex, int overrideLightIndex, int wallLightLevel,
		int mapId, WallLocation location, int textureIndex, float alpha = 1.0f, int addAlpha = 1);
	static void SetWallVertices(DynamicVertexArray& data, const WallVertices& wv, int lightLevelAdd, int lightIndex, int overrideLightIndex, int wallLightLevel,
		int mapId, WallLocation location, int textureIndex, float alpha = 1.0f, int addAlpha = 1);
	static std::unique_ptr<DynamicVertexArray> GetWallVertices(const WallVertices& wv, int lightLevelAdd, int lightIndex, int overrideLightIndex, int wallLightLevel,
		int mapId, WallLocation location, int textureIndex, float alpha = 1.0f, int addAlpha = 1);
	static void GetFlatVertices(DynamicVertexArray& vertices, int startIndex, const TriangulatedWorldVertex& root, const TriangulatedWorldVertex& second,
		const TriangulatedWorldVertex& third, int lightIndex, int overrideLightIndex, int flatLightLevel, int upper, int lower, int addAlpha, float alpha, int textureIndex);
	static int GetWallLightLevel(side_t* side, WallLocation wall);
	void CheckInvalidatedSideCounter(int& sideCounter, int geometryCounter);
	static bool CheckInvalidatedFlatCounter(int& flatCounter, int geometryCounter);
	// [UZDoom] Slope support: Helion walls have one Z per top/bottom edge. Moves the Z of the four corners of a freshly built wall
	// to secplane_t::ZatPoint of the planes that bound it (top/bottom may be null for non-plane bounds).
	// [UZDoom] Fills LightmapU/V/Index of the 6 wall vertices from side_t::lightmap (hw_walls.cpp); leaves them at -1 when the side has none.
	static void SetWallLightmap(DynamicVertex* data, side_t* side, WallLocation location, bool twoSided);
	static void ApplyWallSlopes(DynamicVertex* vertices, int count, const RenderPlane* topPlane, const RenderPlane* bottomPlane);
	// [UZDoom] Lowers the top corners of freshly built walls that are above the given plane to the plane (V keeps the texture scale).
	static void ClipWallTop(DynamicVertex* vertices, int count, const RenderPlane& clipPlane);
	// [UZDoom] ApplyWallSlopes for a wall slice: a sloped plane moves only the corners that sit at the plane's height.
	static void ApplySliceSlopes(DynamicVertex* vertices, int count, const RenderPlane* topPlane, const RenderPlane* bottomPlane);

	// --- GeometryRenderer.Sector3D.cs private
	void RenderSide3D(Sector3D* sector3D, side_t* parentSide, side_t* oppositeParentSide, const RenderSector& wallSector, bool isFront,
		const RenderVertices3DAction& renderVertices);
	std::span<SectorPlane3D> MergePlanes(std::vector<SectorPlane3D>& a, std::vector<SectorPlane3D>& b, Sector3D* ignorePlane);
	void SetWallOffsetFromResult(const RenderWallSliceResult& result, Sector3D* anchorSector3D, float offsetY, const SectorPlane3D& nextPlane3D, double anchorZ, double prevAnchorZ);
	static bool ShouldSkipInsideSlice(const SectorPlane3D& plane3D, const SectorPlane3D& nextPlane3D, const RenderSector& sliceSector);
	// UZDoom: sideSector is the sector of the (fake) side: side->sector, or the parent sector of the 3D sector.
	static void SetWallSliceSector(const RenderSector& sideSector, const RenderSector& wallSector3D, RenderSector& wallSector);
	RenderPlane GetStartAnchorZ(side_t* side, WallLocation wall, const RenderSector& sideSector, const RenderSector& otherSector, const ::Helion::World::Geometry::Sectors::WallHeights* wallHeights3D);
	static RenderPlane CalculateAnchorPlane(side_t* side, WallLocation wall, const RenderSector& sideSector, const RenderSector& otherSector, Sector3D* anchorSector3D);
	// UZDoom: writes the sentinel's tier Y offsets (Helion SideScrollData current) and m_wallScrollY/m_wallScrollPrevY.
	void SetWallOffset(side_t* side, float saveOffsetY, const RenderPlane& top, double anchorZ, double prevAnchorZ, double addOffsetZ = 0, double prevAddOffsetZ = 0);
	static void AddVertices(std::vector<DynamicVertex>& vertices, std::span<DynamicVertex> add);
	// Appends the vertices of a surface to the frame's vertex buffer of renderData; dither: mark the copy (DynamicVertex::Dither).
	void CopyToRenderData(Data::RenderWorldData& renderData, std::span<const DynamicVertex> vertices, bool dither);
	bool IsDistanceCulled(const seg_t* seg) const;
	void RenderCulledWall(seg_t* seg, sector_t* sector);
	void SetSectorToSlice(RenderSector& wallSector, const RenderPlane& top, const RenderPlane& bottom, const ::Helion::World::Geometry::Sectors::WallHeights* wallHeights3D);
	void SetSectorToSlice(RenderSector& wallSector, const ::Helion::World::Geometry::Sectors::WallHeights& heights);
	static void SetSectorToSlice(RenderSector& wallSector, const ::Helion::World::Geometry::Sectors::WallHeights& prevHeights, const ::Helion::World::Geometry::Sectors::WallHeights& heights);
};

}
