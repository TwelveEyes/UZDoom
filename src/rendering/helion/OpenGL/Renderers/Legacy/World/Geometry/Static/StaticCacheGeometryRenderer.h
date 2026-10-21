/*
** StaticCacheGeometryRenderer.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Static.StaticCacheGeometryRenderer
** (+ the Sector3D partial). UZDoom-only partial: StaticCacheGeometryRenderer.Validate.cpp.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Mapping (see the plan's idiom table and RenderStaticState.h):
**  Side / Wall               side_t* + WallLocation (Upper/Middle/Lower = side_t::top/mid/bottom); Wall.Static is
**                            RenderStaticState::Side(side).Static[tier], the walls of 3D floor sides (the fake lines of
**                            Helion) are RenderStaticState::Wall3D(Sector3D::SectorId, side, Middle3D).
**  SectorPlane.Static        RenderStaticState::Plane(RenderPlane::Id).Static
**  Sector                    sector_t* (real) / RenderSector (render view, fake flats)
**  Sector.TaggedSectors3D    m_taggedSectors3D: the Sector3D of every 3D floor, by control sector index (built in UpdateTo)
**  Sector3D.FakeSector       the planes Sector3D::FakeTop / FakeBottom (+Flipped); the fake sector has no lines in UZDoom,
**                            so "the lines of the fake sector" are the lines of Sector3D::ParentSector
**  world events              Helion::World::SectorUpdates events (World/SectorUpdates.h), subscribed in UpdateTo,
**                            unsubscribed in ClearData / the destructor. Helion's SectorMove (every tick of a moving control sector) and
**                            SectorFogColorChanged have no playsim hook: ValidateSignatures raises them.
**  IsMoving / IsPlaneMoving  StaticDataApplier::IsMoving (the SD_Movement bits) / the mover thinker of the plane
*/

#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include "FreeGeometryManager.h"
#include "GeometryData.h"
#include "GeometryTextureLookup.h"
#include "RenderGeometry.h"
#include "OpenGL/Renderers/IStyleRenderer.h"
#include "OpenGL/Renderers/Legacy/World/CoverKey.h"
#include "OpenGL/Renderers/Legacy/World/DynamicVertex.h"
#include "OpenGL/Renderers/Legacy/World/StaticVertex.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryType.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/FloodFill/FloodFillRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Sky/LegacySkyRenderer.h"
#include "OpenGL/Shader/RenderProgram.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"
#include "World/SectorUpdates.h"
#include "World/Static/RenderStaticState.h"

struct FLevelLocals;
struct sector_t;
struct side_t;
struct line_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

class StaticCacheGeometryRenderer : public ::Helion::Render::OpenGL::Renderers::StyleRendererBase
{
public:
	using RenderSector = ::Helion::World::Geometry::Sectors::RenderSector;
	using RenderPlane = ::Helion::World::Geometry::Sectors::RenderPlane;
	using SectorPlanes = ::Helion::World::Geometry::Sectors::SectorPlanes;
	using SectorPlaneFace = ::Helion::World::Geometry::Sectors::SectorPlaneFace;
	using TransferHeightView = ::Helion::World::Geometry::Sectors::TransferHeightView;
	using Sector3D = ::Helion::World::Geometry::Sectors::Sector3D;
	using WallLocation = ::Helion::World::Geometry::Walls::WallLocation;
	using SideTexture = ::Helion::World::Static::SideTexture;
	using StaticGeometryData = ::Helion::World::Static::StaticGeometryData;
	using GLLegacyTexture = Texture::Legacy::GLLegacyTexture;
	using DynamicVertexSpan = std::span<DynamicVertex>;

	StaticCacheGeometryRenderer(Texture::Legacy::LegacyGLTextureManager& textureManager, ::Helion::Render::OpenGL::Shader::RenderProgram& program,
		GeometryRenderer& geometryRenderer);
	~StaticCacheGeometryRenderer() override;

	StaticCacheGeometryRenderer(const StaticCacheGeometryRenderer&) = delete;
	StaticCacheGeometryRenderer& operator=(const StaticCacheGeometryRenderer&) = delete;

	// The flood fill state (RenderStaticState), the sector signatures and every cached vertex are rebuilt for the level.
	// GeometryRenderer::UpdateTo calls, in this order: RenderStaticState::Reset(world), SetFloodSectors, Sector3DTable().UpdateTo,
	// Portals().UpdateTo, StaticRenderer().UpdateTo.
	void UpdateTo(FLevelLocals* world, bool sameAsPreviousMap = false);

	void Render(Data::RenderDataStyle style) override;
	bool HasStyleToRender(Data::RenderDataStyle style) override;

	void RenderWalls();
	void RenderTwoSidedMiddleWalls();
	void RenderMiddle3D();
	void RenderFlats();
	void Render(GeometryType type);
	void RenderCoverWalls();
	void RenderOneSidedCoverWalls();
	void RenderCoverFlats();
	void RenderSkies(Shared::RenderInfo& renderInfo);
	Sky::LegacySkyRenderer& SkyRenderer() { return *m_skyRenderer; }

	void CheckForFloodFill(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFront);

	// UZDoom-only (StaticCacheGeometryRenderer.Validate.cpp), plan D14: call once per frame before anything is rendered.
	// Compares sector_t::renderSignature / side_t::renderSignature with the current state and routes mismatches through the
	// same handlers as the events.
	void ValidateSignatures();

	void Dispose();

private:
	static constexpr int WallVertices = 6;
	// UZDoom: portal planes (SD_Portal) are drawn with the alpha / skip rules of the portal driver, never cached.
	static constexpr uint8_t IgnoreFlags = SD_Movement | SD_Portal;

	// A wall: the Static slot plus what Helion reads off its Wall object.
	struct WallRef
	{
		side_t* Side;	// the side used for type queries (the fog barrier uses GeometryRenderer::FogSide())
		WallLocation Location;
		int TextureHandle;
		StaticGeometryData* Static;
		bool AllowNullTexture = false;
	};

	Texture::Legacy::LegacyGLTextureManager& m_textureManager;
	GeometryRenderer& m_geometryRenderer;
	Portals::FloodFill::FloodFillRenderer* m_floodFillRenderer;
	::Helion::Render::OpenGL::Shader::RenderProgram& m_program;
	RenderGeometry m_geometry;
	GeometryTextureLookup m_textureToGeometryLookup;
	FreeGeometryManager m_freeManager;
	std::unique_ptr<Sky::LegacySkyRenderer> m_skyRenderer;

	// control sector index -> sectors with transfer heights (heightsec) controlled by it
	std::unordered_map<int, std::vector<sector_t*>> m_transferHeightsLookup;
	// control sector index -> Sector3D of the 3D floors it controls (Helion Sector.TaggedSectors3D)
	std::unordered_map<int, std::vector<Sector3D*>> m_taggedSectors3D;
	GeometryRenderer::RenderCoverWallAction m_renderCoverWallAction;
	GeometryRenderer::RenderVertices3DAction m_renderSectorWallVertices3D;
	DynamicVertex m_coverWallVertices3D[6] = {};

	std::unordered_map<CoverKey, StaticGeometryData, CoverKeyHash> m_coverWallLookup;
	std::unordered_map<CoverKey, StaticGeometryData, CoverKeyHash> m_coverFlatLookup;

	std::unique_ptr<GeometryData> m_coverWallGeometry;
	std::unique_ptr<GeometryData> m_coverWallGeometryOneSided;
	std::unique_ptr<GeometryData> m_coverFlatGeometry;

	FLevelLocals* m_world = nullptr;
	bool m_disposed = false;
	bool m_worldReload = false;
	bool m_vanillaRender = false;

	// event subscriptions (Event::Subscribe tokens), 0 = not subscribed
	int m_tokenMoveStart = 0;
	int m_tokenMoveComplete = 0;
	int m_tokenTransformed = 0;
	int m_tokenPlaneTexture = 0;
	int m_tokenSideTexture = 0;

	// UZDoom-only: false until the signatures were taken at the end of UpdateTo
	bool m_signaturesValid = false;

	static int TransparentGeometryCompare(const GeometryData& x, const GeometryData& y);

	void SetupCoverGeometry(FLevelLocals* world, bool sameAsPreviousMap);
	void Subscribe();
	void Unsubscribe();

	// --- events
	void World_SectorMove(sector_t* sector);
	void World_SectorMoveStart(sector_t* sector, int pos);
	void World_SectorMoveComplete(sector_t* sector, int pos);
	void World_SectorPlaneTransformed(sector_t* sector, int pos);
	// Rebuilds the cached geometry of one plane in place (no line / move-state handling).
	void RefreshStaticPlane(sector_t* sector, int pos);
	void World_SideTextureChanged(side_t* side, int tier, FTextureID textureId, FTextureID previousTextureId);
	void World_PlaneTextureChanged(sector_t* sector, int pos, FTextureID textureId, FTextureID previousTextureId);
	void World_SectorFogColorChanged(sector_t* sector, uint32_t previousFade);

	// --- flood fill
	void UpdateSectorPlaneFloodFill(line_t* line);
	void UpdateSectorPlaneFloodFill(side_t* facingSide, const RenderSector& facingSector, bool isFront);
	void UpdateFloodFillSideState(side_t* facingSide, side_t* otherSide, const RenderSector& otherSector, bool isFront, SideTexture previous, SideTexture sideTexture);
	void AddFloodFillPlane(side_t* side, const RenderSector& sector, bool isFrontSide);

	// --- adding geometry
	void AddTransferSector(sector_t* sector);
	void AddLine(line_t* line, bool update = false);
	void AddOneSided(side_t* side, bool isFrontSide, bool update);
	void AddTwoSided(side_t* side, bool isFrontSide, bool update, bool fogBarrierOnly = false);
	void UpdateFogBarrier(side_t* side, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide);
	bool ShouldRenderStaticMiddle(side_t* side);
	// ceilingSky: UZDoom, a lower wall's sky side (GeometryRenderer::RenderSkySide) belongs to the ceiling sky of `sector`.
	void AddSkyGeometry(side_t* side, WallLocation wallLocation, const RenderPlane* plane, const SkyVertexArray* vertices, const RenderSector& sector, bool update,
		bool ceilingSky = false);
	static void AddVertices(TArray<StaticVertex>& staticVertices, DynamicVertexSpan vertices);
	static void CopyVertices(StaticVertex* staticVertices, DynamicVertexSpan vertices, int index);

	WallRef MakeWall(side_t* side, WallLocation location);
	WallRef MakeFogWall(side_t* side);
	void SetSideVertices(const WallRef& wall, bool update, DynamicVertexSpan sideVertices, bool visible, bool repeatY, Sector3D* sector3D);
	void SetSideData(StaticGeometryData& staticGeometry, GeometryType type, int textureHandle, int vboIndex, int vertexCount, bool repeatY, GeometryData* geometryData);
	TArray<StaticVertex>& GetTextureVertices(GeometryType type, int textureHandle, bool repeatY);
	std::unique_ptr<GeometryData> CreateGeometryData(GeometryType type, int textureHandle, bool repeat, int vboSize, GLLegacyTexture* overrideTexture, const FString* label);
	GeometryData* AllocateGeometryData(GeometryType type, int textureHandle, bool repeat, GLLegacyTexture* overrideTexture = nullptr);
	void ClearData(FLevelLocals* world, bool sameAsPreviousMap);
	static void ClearVbo(Buffer::Array::Vertex::VertexBufferObject<StaticVertex>* vbo);

	// real sector plane (sector_t::floor / sector_t::ceiling) or, with geometryPlane / renderPlane, a 3D floor plane.
	// renderPlane = the plane to draw (default: the plane of renderSector at face), geometryPlane = the plane owning the Static data (default: the real plane).
	void AddSectorPlane(sector_t* sectorForSubsectors, SectorPlaneFace face, bool floor, bool update = false, const RenderSector* renderSector = nullptr,
		const RenderSector* lightLevelSector = nullptr, const RenderPlane* geometryPlane = nullptr, bool allowAlpha = false, Sector3D* sector3D = nullptr,
		const RenderPlane* renderPlaneOverride = nullptr);

	static void RenderGeometryList(Texture::Legacy::LegacyGLTextureManager& textureManager, RenderGeometry::GeometryList& geometry);
	void RenderGeometryTypes(RenderGeometry::GeometryList& geometry);
	static void RenderCoverInternal(GeometryData* data);

	// --- movement
	bool HandleSectorMoveStart(sector_t* sector, int pos, bool check3D = true, bool checkMovement = true, bool handlePlane = true);
	void HandleSectorMoveStartForLines(sector_t* sector, bool checkOpposingSector3D);
	void HandleSectorMoveStartForLines3D(Sector3D* sector3D);
	void HandleSectorMoveStart3D(Sector3D* sector3D);
	bool HandleSector3DPlaneMoveStart(Sector3D* sector3D, const RenderPlane& fakePlane, bool checkMovement = true);
	void ClearSideGeometryVertices(side_t* side, WallLocation location);
	void HandleSectorMoveComplete(sector_t* sector, int pos, bool check3D = true, bool handlePlane = true);
	void HandleSectorMoveComplete3D(Sector3D* sector3D);
	void HandleSectorMoveCompleteForLines(sector_t* sector, bool checkOpposingSector3D);
	static bool HasOtherMovementSector3D(Sector3D* sector3D);
	static bool SectorLinesProcessed(::Helion::World::Static::RenderSectorState& state, ::Helion::World::Static::MoveState moveState);
	static bool IsPlaneMoving(const sector_t* sector, int pos);
	// The fake plane of a 3D floor that follows the plane of the control sector (pos = sector_t::floor / ceiling), see the file comment.
	static const RenderPlane& FakePlane(const Sector3D* sector3D, int controlPos, bool flipped);
	static bool HasFakePlane(const Sector3D* sector3D, int controlPos, bool flipped);

	static bool ClearGeometryVertices(const StaticGeometryData& data);
	static void ClearGeometryVerticesRaw(GeometryData& geometryData, int startIndex, int length);
	static bool GetRepeatY(const side_t* side, WallLocation location);
	static int TierOf(WallLocation location);
	static WallLocation LocationOf(int tier);

	void UpdateVertices(StaticGeometryData& staticGeometry, int textureHandle, DynamicVertexSpan vertices, const RenderPlane* plane, side_t* side,
		WallLocation wallLocation, bool repeat, GLLegacyTexture* texture = nullptr, Sector3D* sector3D = nullptr, bool isOneSidedLine = false);
	void AddOrUpdateCoverWall(side_t* side, DynamicVertexSpan sideVertices, WallLocation location, bool oneSided);
	DynamicVertexSpan CreateMiddleCoverWall3D(DynamicVertexSpan sideVertices);
	void HandleCoverWallUpload(Buffer::Array::Vertex::VertexBufferObject<StaticVertex>& geometryVbo, int index, int length);
	static void EnsureCoverWallVboCapacity(const StaticGeometryData& staticGeometryData, DynamicVertexSpan sideVertices, Buffer::Array::Vertex::VertexBufferObject<StaticVertex>& geometryVbo);
	void AddOrUpdateCoverFlatGeometry(sector_t* sector, const RenderPlane& plane, const DynamicVertexArray& vertices);
	void AddNewGeometry(int textureHandle, DynamicVertexSpan vertices, GeometryType geometryType, StaticGeometryData& staticGeometry, const RenderPlane* plane, side_t* side,
		WallLocation wallLocation, bool repeatY, GLLegacyTexture* texture, Sector3D* sector3D);
	void SetRuntimeGeometryData(StaticGeometryData& staticGeometry, side_t* side, WallLocation wallLocation, int textureHandle, GeometryData* geometryData,
		DynamicVertexSpan vertices, bool repeat, Sector3D* sector3D);
	static FString GetGeometryLabel(GeometryType type, int textureHandle, bool repeat);
	static void EnsureCapacity(TArray<StaticVertex>& data, int capacity);
	StaticGeometryData& PlaneStatic(const RenderPlane& plane) { return ::Helion::World::Static::RenderStaticState::Plane(plane.Id).Static; }
	static int TextureHandleOf(const GLLegacyTexture* texture);
	static bool IsSkyTexture(int textureHandle);
	static int FlipPos(int pos) { return pos == sector_t::floor ? sector_t::ceiling : sector_t::floor; }
	int GameTick() const { return ::Helion::World::Static::RenderStaticState::GameTick(); }

	// --- StaticCacheGeometryRenderer.Sector3D.cs
	void AddSectors3D(sector_t* sector, bool update);
	void AddSector3D(Sector3D* sector3D, SectorPlanes planes, bool update, bool clearWallVertices = false);
	void AddSectorPlanes3D(Sector3D* sector3D, SectorPlanes planes, bool update);
	void RenderSectorWallVertices3D(side_t* side, WallLocation wall, const RenderSector& wallSector, GLLegacyTexture* texture, DynamicVertexSpan vertices, Sector3D* sector3D);

	// --- StaticCacheGeometryRenderer.Validate.cpp
	struct SignatureSnapshot;
	void TakeSectorSignature(sector_t* sector, FSectorRenderSignature& signature);
	void TakeSideSignature(side_t* side, FSideRenderSignature& signature);
	void InitSignatures();
	// Stores the live values of one plane / tier in the cached signature after an event handled them, so the sweep does not handle them twice.
	void RefreshPlaneSignature(sector_t* sector, int pos);
	void RefreshSideSignature(side_t* side, int tier);
	// True when the live plane values (height, texture, transform) differ from the cached signature, or no signatures exist yet.
	bool PlaneSignatureChanged(sector_t* sector, int pos);
};

}
