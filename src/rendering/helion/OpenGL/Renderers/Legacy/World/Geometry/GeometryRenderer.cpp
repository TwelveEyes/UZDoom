/*
** GeometryRenderer.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.GeometryRenderer (GeometryRenderer.cs lines 1-1200).
** The rest of the file lives in the Part B / Part C fragments pasted at the end of this namespace.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** UZDoom differences (the plan's Type and idiom mapping applies):
**  - Helion's per side / plane render counters (LastRenderGametick, *InvalidatedCount) live in the renderer
**    (SideCounters / PlaneCounters), the static cache state (LastRenderChangeGametick, flood keys) in RenderStaticState.
**  - Helion's compatibility map definition (NoRenderFloorSectors, ...) does not exist: plane NoRender comes from
**    PLANEF_NORENDER and the mid texture hack is applied to every sector (hw_renderhacks.cpp does that for every map).
**  - Helion's island geometry (FloodSectors / BadSubsectors) does not exist, flood flags come from the flood fill input
**    API of RenderStaticState (SetSectorFlood).
**  - Helion's per sector subsector lists are FSection groups (m_sectionsBySector).
**  - Sector3D.FakeSector does not exist: 3D floors are rendered through RenderSector3D / RenderSectorFlats3D.
*/

#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_set>
#include "doomdata.h"
#include "g_levellocals.h"
#include "r_defs.h"
#include "p_lnspec.h"
#include "r_sky.h"
#include "r_utility.h"
#include "texturemanager.h"
#include "gametexture.h"
#include "textureid.h"
#include "r_data/r_sections.h"
#include "gamedata/textures/animations.h"
#include "HelionCVars.h"
#include "Renderer.h"
#include "RenderBlock.h"
#include "World/WorldStatic.h"
#include "World/Static/RenderStaticState.h"
#include "Common/Shared/World/WorldTriangulator.h"
#include "OpenGL/Renderers/Legacy/World/VertexOptions.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Static/LightBufferType.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Static/StaticCacheGeometryRenderer.h"
#include "World/Static/StaticDataApplier.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalDriver.h"
#include "OpenGL/Texture/Legacy/GLTextureArrayBuilder.h"
#include "OpenGL/Renderers/Legacy/World/DitherTransparency.h"

EXTERN_CVAR(Int, r_fakecontrast)
CVAR(Bool, gl_no_skyclear, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

using ::Helion::Render::Common::Shared::World::MidTexOpening;
using ::Helion::Render::OpenGL::Shared::World::WorldTriangulator;
using ::Helion::Render::RenderBlock;
using ::Helion::World::Geometry::Sectors::SectorPlaneFace;
using ::Helion::World::Geometry::Sectors::SectorPlanes;
using ::Helion::World::Geometry::Sectors::TransferHeightView;
using ::Helion::World::Static::RenderStaticState;
using ::Helion::World::Static::SideTexture;
using ::Helion::World::Geometry::Walls::WallLocation;
using ::Helion::World::WorldStatic;
using Data::RenderDataStyle;
using Static::LightBufferType;

namespace
{

// Helion Config.Render.TextureTransparency (default true). Not exposed as a CVar.
constexpr bool TextureTransparency = true;

// Helion Side.PartnerSide
side_t* PartnerSideOf(const side_t* side)
{
	line_t* line = side->linedef;
	return line->sidedef[0] == side ? line->sidedef[1] : line->sidedef[0];
}

// Helion Side.Line.Alpha: the side's own alpha overrides the line alpha.
double SideLineAlpha(side_t* side)
{
	return side->HasAlpha() ? side->alpha : side->linedef->alpha;
}

// Helion LineOpening.GetOpeningHeight
double GetOpeningHeight(line_t* line)
{
	sector_t* front = line->frontsector;
	sector_t* back = line->backsector;
	return std::min(front->GetPlaneTexZ(sector_t::ceiling), back->GetPlaneTexZ(sector_t::ceiling)) -
		std::max(front->GetPlaneTexZ(sector_t::floor), back->GetPlaneTexZ(sector_t::floor));
}

// Helion Seg2D.OnRight
bool OnRight(const line_t* line, const Helion::Geometry::Vec2D& point)
{
	DVector2 delta = line->Delta();
	return (delta.X * (point.Y - line->v1->fY())) - (delta.Y * (point.X - line->v1->fX())) <= 0;
}

// UZDoom: Helion Sector.IsFloorStatic / IsCeilingStatic of a (fake) plane. The control sector of a 3D plane carries the dynamic bits.
bool PlaneIsStatic(const ::Helion::World::Geometry::Sectors::RenderPlane& plane)
{
	return plane.Sector == nullptr || plane.Sector->renderDynamic[(int)plane.Facing] == 0;
}

// Helion Sector.AreFlatsStatic
bool AreFlatsStatic(const sector_t* sector)
{
	return sector->renderDynamic[sector_t::floor] == 0 && sector->renderDynamic[sector_t::ceiling] == 0;
}

// The sentinel sides are only ever compared by pointer, give them sane scales so that nothing divides by zero.
void InitSentinelSide(side_t& side)
{
	for (auto& part : side.textures)
	{
		part.xScale = 1;
		part.yScale = 1;
	}
	side.alpha = 1;
}

void SortUnique(std::vector<int>& values)
{
	std::sort(values.begin(), values.end());
	values.erase(std::unique(values.begin(), values.end()), values.end());
}

}

GeometryRenderer::GeometryRenderer(Texture::Legacy::LegacyGLTextureManager* glTextureManager, ::Helion::Render::OpenGL::Shader::RenderProgram* program,
	::Helion::Render::OpenGL::Shader::RenderProgram* staticProgram, Data::RenderWorldDataManager* worldDataManager, Shared::World::ViewClipping::ViewClipper* viewClipper,
	Shared::World::ViewClipping::ViewClipper* viewClipperPrev) :
	m_program(program),
	m_glTextureManager(glTextureManager),
	m_worldDataManager(worldDataManager),
	m_skyRenderer(std::make_unique<Sky::LegacySkyRenderer>(glTextureManager)),
	m_viewClipper(viewClipper),
	m_viewClipperPrev(viewClipperPrev)
{
	// UZDoom: the fake / fog / empty traverse sides are only sentinels (IsFogSide, m_fakeSide): zero-initialised dummy sides that never
	// touch the level.
	m_fakeSide = std::make_unique<side_t>();
	InitSentinelSide(*m_fakeSide);
	m_emptyTraverseSide = std::make_unique<side_t>();
	InitSentinelSide(*m_emptyTraverseSide);
	m_fogSide = std::make_unique<side_t>();
	InitSentinelSide(*m_fogSide);
	m_fogSide->Flags |= WALLF_WRAP_MIDTEX;

	m_portalsOwner = std::make_unique<Portals::PortalRenderer>(*m_glTextureManager);
	m_portals = m_portalsOwner.get();
	m_staticCacheOwner = std::make_unique<Static::StaticCacheGeometryRenderer>(*m_glTextureManager, *staticProgram, *this);
	m_staticCacheGeometryRenderer = m_staticCacheOwner.get();
	m_renderCoverWallAction = [this](side_t* side, std::span<DynamicVertex> vertices, WallLocation location, bool oneSided)
	{
		m_worldDataManager->AddCoverWallVertices(side, vertices, location, oneSided);
	};

	m_renderOneSidedSliceFunc = [this](const RenderWallSliceArgs& args) { return RenderOneSidedSlice(args); };
	m_renderTwoSidedLowerSliceFunc = [this](const RenderWallSliceArgs& args) { return RenderTwoSidedLowerSlice(args); };
	m_renderTwoSidedUpperSliceFunc = [this](const RenderWallSliceArgs& args) { return RenderTwoSidedUpperSlice(args); };
	m_renderTwoSidedMiddleSliceFunc = [this](const RenderWallSliceArgs& args) { return RenderTwoSidedMiddleSlice(args); };
	m_renderSectorSliceFunc3D = [this](const RenderWallSliceArgs& args) { return RenderSectorSlice3D(args); };

	float options = VertexOptions::PackSurface(1, 1, 0, 0, 0, 0, 0);
	for (size_t i = 0; i < m_wallVertices.size(); i++)
		m_wallVertices[i].SurfaceOptions = options;
}

GeometryRenderer::~GeometryRenderer()
{
	Dispose();
}

void GeometryRenderer::UpdateTo(FLevelLocals* world, bool sameAsPreviousMap)
{
	m_world = world;
	if (!sameAsPreviousMap)
	{
		m_skyRenderer->Reset();
		m_worldDataManager->Reset();
		m_hitLines.assign(world->lines.Size(), 0);
	}

	m_vanillaRender = gl_vanillarender;
	m_pixelGapCorrection = gl_pixelgapcorrection;

	int sideCount = (int)world->sides.Size();
	int sectorCount = (int)world->sectors.Size();
	bool freeData = !sameAsPreviousMap;
	UpdateVertexWallLookup(m_vertexLookup, sideCount, freeData);
	UpdateVertexWallLookup(m_vertexLowerLookup, sideCount, freeData);
	UpdateVertexWallLookup(m_vertexUpperLookup, sideCount, freeData);
	UpdateSkyWallLookup(m_skyWallVertexLowerLookup, sideCount, freeData);
	UpdateSkyWallLookup(m_skyWallVertexUpperLookup, sideCount, freeData);
	UpdateFlatVertices(m_vertexFloorLookup, sectorCount, freeData);
	UpdateFlatVertices(m_vertexCeilingLookup, sectorCount, freeData);
	UpdateSkyFlatVertices(m_skyFloorVertexLookup, sectorCount, freeData);
	UpdateSkyFlatVertices(m_skyCeilingVertexLookup, sectorCount, freeData);

	m_vertexPlaneLookup3D.clear();
	m_noCacheData.clear();
	m_flatInvalidatedCounts.clear();

	m_invalidatedCounter = 1;

	// UZDoom: Helion keeps these counters on Side / SectorPlane and resets them with the world objects.
	m_sideCounters.assign(sideCount, SideRenderCounters());
	m_planeCounters.assign((size_t)sectorCount * 2, PlaneRenderCounters());
	m_planeCounters3D.clear();

	// List of each section mapped to a sector id (Helion: list of each subsector mapped to a sector id).
	m_sectionsBySector.Clear();
	m_sectionsBySector.Resize(sectorCount);
	for (int i = 0; i < sectorCount; i++)
	{
		TArrayView<FSection> sections = world->sections.SectionsForSector(i);
		for (unsigned j = 0; j < sections.Size(); j++)
			m_sectionsBySector[i].Push(&sections[j]);
	}

	// Clears the flood state, so it has to run before anything writes it (MidTextureHack, SetFloodSectors).
	RenderStaticState::Reset(world);

	m_sector3DTable.UpdateTo(world);

	LoadTextureCounts loadTextureCounts;
	if (!sameAsPreviousMap)
		loadTextureCounts = PreloadAllTextures(world);

	m_drawnSides.assign(sideCount, 0);
	m_contrastMode = r_fakecontrast;

	Clear(m_tickFraction, true);
	SetRenderCompatibility(world);
	SetFloodSectors(world);

	if (!sameAsPreviousMap)
	{
		Texture::Legacy::GLTextureArrayBuilder::BuildLevel(*m_glTextureManager, loadTextureCounts.FlatTextures,
			loadTextureCounts.WallTexturesRepeat, loadTextureCounts.WallTexturesClamp);
	}

	m_portals->UpdateTo(world);
	m_staticCacheGeometryRenderer->UpdateTo(world, sameAsPreviousMap);
	m_worldDataManager->InitCoverWallRenderData(m_glTextureManager->WhiteTexture, m_program);
}

void GeometryRenderer::UpdateVertexWallLookup(std::vector<std::unique_ptr<DynamicVertexArray>>& vertices, int sideCount, bool free)
{
	for (size_t i = 0; i < vertices.size(); i++)
	{
		auto& data = vertices[i];
		if (data == nullptr)
			continue;

		if (free)
		{
			data.reset();
			continue;
		}

		std::fill(data->begin(), data->end(), DynamicVertex());
	}

	if (vertices.size() < (size_t)sideCount)
	{
		vertices.clear();
		vertices.resize(sideCount);
	}
}

void GeometryRenderer::UpdateSkyWallLookup(std::vector<std::unique_ptr<SkyVertexArray>>& vertices, int sideCount, bool free)
{
	for (size_t i = 0; i < vertices.size(); i++)
	{
		auto& data = vertices[i];
		if (data == nullptr)
			continue;

		if (free)
		{
			data.reset();
			continue;
		}

		std::fill(data->begin(), data->end(), SkyGeometryVertex());
	}

	if (vertices.size() < (size_t)sideCount)
	{
		vertices.clear();
		vertices.resize(sideCount);
	}
}

void GeometryRenderer::UpdateFlatVertices(std::vector<std::unique_ptr<DynamicVertexArray>>& vertices, int sectorCount, bool free)
{
	for (size_t i = 0; i < vertices.size(); i++)
	{
		auto& data = vertices[i];
		if (data == nullptr)
			continue;

		if (free)
		{
			data.reset();
			continue;
		}

		std::fill(data->begin(), data->end(), DynamicVertex());
	}

	if (vertices.size() < (size_t)sectorCount)
	{
		vertices.clear();
		vertices.resize(sectorCount);
	}
}

void GeometryRenderer::UpdateSkyFlatVertices(std::vector<std::unique_ptr<SkyVertexArray>>& vertices, int sectorCount, bool free)
{
	for (size_t i = 0; i < vertices.size(); i++)
	{
		auto& data = vertices[i];
		if (data == nullptr)
			continue;

		if (free)
		{
			data.reset();
			continue;
		}

		std::fill(data->begin(), data->end(), SkyGeometryVertex());
	}

	if (vertices.size() < (size_t)sectorCount)
	{
		vertices.clear();
		vertices.resize(sectorCount);
	}
}

void GeometryRenderer::SetRenderCompatibility(FLevelLocals* world)
{
	// UZDoom: there is no compatibility map definition (NoRenderFloorSectors / NoRenderCeilingSectors / MidTextureHackSectors).
	// Plane NoRender is PLANEF_NORENDER (RenderPlane::NoRender). The mid texture hack is applied to every sector like hw_renderhacks.cpp does.
	m_midTextureHack.Apply(world, m_glTextureManager, *this);
}

void GeometryRenderer::SetFloodSectors(FLevelLocals* world)
{
	// Helion: sector.Flood = IslandGeometry.FloodSectors, subsector.Flood = IslandGeometry.BadSubsectors (malformed subsectors found by the
	// island classifier, plus the sector containing them). UZDoom's equivalent of a malformed subsector is the open sector render hack
	// that MapLoader::PrepareSectorData marks with subsector_t::hacked & 1 (subsectors joined by minisegs to another render sector:
	// deep water / invisible floors). hw_renderhacks.cpp floods those with the anchor's plane, never on Hexen format maps
	// (classic AddHackedSubsector). The sectors on the other side of the minisegs stand in for Helion's containing sector.
	if (world->maptype == MAPTYPE_HEXEN)
		return;

	for (subsector_t& subsector : world->subsectors)
	{
		if ((subsector.hacked & 1) == 0)
			continue;

		RenderStaticState::SetSubsectorFlood(&subsector, true);
		RenderStaticState::SetSectorFlood(subsector.render_sector, true);

		for (uint32_t i = 0; i < subsector.numlines; i++)
		{
			seg_t* seg = subsector.firstline + i;
			if (seg->linedef != nullptr || seg->PartnerSeg == nullptr || seg->PartnerSeg->Subsector == nullptr)
				continue;

			sector_t* other = seg->PartnerSeg->Subsector->render_sector;
			if (other != nullptr && other != subsector.render_sector)
				RenderStaticState::SetSectorFlood(other, true);
		}
	}
}

void GeometryRenderer::Clear(double tickFraction, bool newTick)
{
	m_tickFraction = tickFraction;
	if (newTick)
		m_skyRenderer->Clear();
}

void GeometryRenderer::ClearBsp()
{
	std::fill(m_hitLines.begin(), m_hitLines.end(), 0);
}

void GeometryRenderer::RenderStaticGeometryWalls()
{
	m_staticCacheGeometryRenderer->RenderWalls();
}

void GeometryRenderer::RenderStaticGeometryFlats()
{
	m_staticCacheGeometryRenderer->RenderFlats();
}

void GeometryRenderer::RenderStaticCoverWalls()
{
	m_staticCacheGeometryRenderer->RenderCoverWalls();
}

void GeometryRenderer::RenderStaticOneSidedCoverWalls()
{
	m_staticCacheGeometryRenderer->RenderOneSidedCoverWalls();
}

void GeometryRenderer::RenderStaticTwoSidedWalls()
{
	m_staticCacheGeometryRenderer->RenderTwoSidedMiddleWalls();
}

void GeometryRenderer::RenderStaticMiddle3D()
{
	m_staticCacheGeometryRenderer->RenderMiddle3D();
}

void GeometryRenderer::RenderStaticSkies(Shared::RenderInfo& renderInfo)
{
	m_staticCacheGeometryRenderer->RenderSkies(renderInfo);
}

void GeometryRenderer::RenderSkies(Shared::RenderInfo& renderInfo, bool renderStatic, bool staticSkyVisible)
{
	// UZDoom (classic DrawScene, FPortalSceneState::RenderFirstSkyPortal): unless gl_no_skyclear is set the classic renderer
	// draws one sky portal before the scene without a stencil (with one only in out of body views, which this skips), so what no
	// geometry covers shows that sky. Classic picks the portal whose lines contain the viewpoint, else the one with the most
	// lines; here that is the sky of the view sector, else the first sky with geometry. Inside a portal view the stencil holds
	// the portal mask, so this is the main view only.
	// Classic only has a sky portal when the walk saw a sky: the BSP walk gathers only seen sky geometry, the static mode keeps
	// the sky geometry of the whole map, so there the heuristics walk of the tick (BspHeuristics::SkyVisible) decides.
	Portals::PortalDriver* portals = Portals::PortalDriver::Instance();
	if (!gl_no_skyclear && !r_viewpoint.bDoOob && (portals == nullptr || !portals->InPortal()) && (!renderStatic || staticSkyVisible))
	{
		Sky::LegacySkyRenderer& staticSkies = m_staticCacheGeometryRenderer->SkyRenderer();
		Sky::ISkyComponent* first = m_skyRenderer->FirstWithGeometry();
		if (first == nullptr && renderStatic)
			first = staticSkies.FirstWithGeometry();

		sector_t* viewSector = renderInfo.ViewSector;
		if (first != nullptr && viewSector != nullptr)
		{
			for (int plane : { sector_t::ceiling, sector_t::floor })
			{
				if (viewSector->GetTexture(plane) == skyflatnum && !Portals::PortalDriver::PlaneIsPortal(viewSector, plane))
				{
					Sky::ISkyComponent* viewSky;
					if (m_skyRenderer->GetOrCreateSectorSky(viewSector, plane, viewSky))
						first = viewSky;
					break;
				}
			}
		}

		if (first != nullptr)
			Sky::LegacySkyRenderer::RenderUnmasked(*first, renderInfo);
	}

	m_skyRenderer->Render(renderInfo);
}

void GeometryRenderer::RenderPortals(Shared::RenderInfo& renderInfo)
{
	m_portals->Render(renderInfo);
}

void GeometryRenderer::RenderWallClipPortals(Shared::RenderInfo& renderInfo)
{
	m_portals->RenderWallClip(renderInfo);
}

void GeometryRenderer::RenderStaticStyle(Data::RenderDataStyle style)
{
	m_staticCacheGeometryRenderer->Render(ToGeometryType(style));
}

TArrayView<FSection*> GeometryRenderer::SectionsOfSector(const sector_t* sector)
{
	TArray<FSection*>& sections = m_sectionsBySector[sector->Index()];
	return TArrayView<FSection*>(sections.Data(), sections.Size());
}

bool GeometryRenderer::IsSkyTexture(int textureHandle)
{
	return textureHandle == skyflatnum.GetIndex();
}

int GeometryRenderer::Gametick()
{
	return RenderStaticState::GameTick();
}

bool GeometryRenderer::IsLevelSide(const side_t* side) const
{
	return side != m_fogSide.get() && side != m_fakeSide.get() && side != m_emptyTraverseSide.get();
}

GeometryRenderer::SideRenderCounters& GeometryRenderer::SideCounters(const side_t* side)
{
	if (!IsLevelSide(side))
		return m_scratchSideCounters;
	return m_sideCounters[side->Index()];
}

GeometryRenderer::PlaneRenderCounters& GeometryRenderer::PlaneCounters(int planeId)
{
	if ((size_t)planeId < m_planeCounters.size())
		return m_planeCounters[planeId];
	return m_planeCounters3D[planeId];
}

bool GeometryRenderer::SideOffsetChanged(const side_t* side) const
{
	// Helion Side.OffsetChanged (ScrollData != null): the sentinel sides always count as changed.
	if (!IsLevelSide(side))
		return true;
	return (side->renderDynamic & (SD_Scroll | SD_ScrollY)) != 0;
}

int& GeometryRenderer::SideInvalidatedCount(const side_t* side, WallLocation tier)
{
	SideRenderCounters& counters = SideCounters(side);
	switch (tier)
	{
	case WallLocation::Upper:
		return counters.UpperInvalidatedCount;
	case WallLocation::Lower:
		return counters.LowerInvalidatedCount;
	default:
		return counters.MiddleInvalidatedCount;
	}
}

bool GeometryRenderer::CheckSideRenderingChanged(const side_t* side)
{
	// The sentinel sides have LastRenderGametick = -1 in Helion: always changed.
	if (!IsLevelSide(side))
		return true;
	return RenderStaticState::Side(side).LastRenderChangeGametick >= SideCounters(side).LastRenderGametick;
}

bool GeometryRenderer::CheckPlaneRenderingChanged(const sector_t* sector, int pos)
{
	if (RenderStaticState::Plane(sector, pos).LastRenderChangeGametick >= PlaneCounters(sector->Index() * 2 + pos).LastRenderGametick - 1)
		return true;

	double z = sector->GetPlaneTexZ(pos);
	if (::Helion::World::Geometry::Sectors::PrevPlaneTexZ(const_cast<sector_t*>(sector), pos, z) != z)
		return true;

	// Helion RenderOffsets.Gametick != 0
	return (sector->renderDynamic[pos] & (SD_Scroll | SD_ScrollY)) != 0;
}

sector_t* GeometryRenderer::RealSectorOfPlane(const RenderPlane& plane)
{
	unsigned index = (unsigned)plane.Id >> 1;
	if (index < m_world->sectors.Size())
		return &m_world->sectors[index];
	return plane.Sector;
}

bool GeometryRenderer::CheckPlaneRenderingChanged(const RenderPlane& plane)
{
	unsigned index = (unsigned)plane.Id >> 1;
	if (index < m_world->sectors.Size())
	{
		if (RenderStaticState::Plane(&m_world->sectors[index], plane.Id & 1).LastRenderChangeGametick >= PlaneCounters(plane.Id).LastRenderGametick - 1)
			return true;
	}

	return plane.PrevZ != plane.Z || plane.RenderOffsets.Gametick != 0;
}

bool GeometryRenderer::CheckSectorRenderingChanged(const sector_t* sector, int gametick, bool checkTransferHeights)
{
	for (int pos = 0; pos < 2; pos++)
	{
		double z = sector->GetPlaneTexZ(pos);
		if (RenderStaticState::Plane(sector, pos).LastRenderChangeGametick >= gametick - 1 ||
			::Helion::World::Geometry::Sectors::PrevPlaneTexZ(const_cast<sector_t*>(sector), pos, z) != z)
			return true;
	}

	if (checkTransferHeights)
	{
		sector_t* controlSector = sector->GetHeightSec();
		if (controlSector != nullptr)
			return CheckSectorRenderingChanged(controlSector, gametick, false);
	}

	return false;
}

DynamicVertexArray* GeometryRenderer::GetCachedSide(std::vector<std::unique_ptr<DynamicVertexArray>>& lookup, const side_t* side)
{
	if (!IsLevelSide(side))
		return nullptr;
	return lookup[side->Index()].get();
}

void GeometryRenderer::SetCachedSide(std::vector<std::unique_ptr<DynamicVertexArray>>& lookup, const side_t* side, std::unique_ptr<DynamicVertexArray> data)
{
	if (IsLevelSide(side))
		lookup[side->Index()] = std::move(data);
	else
		m_noCacheData[&lookup] = std::move(data);
}

void GeometryRenderer::CopyToRenderData(Data::RenderWorldData& renderData, std::span<const DynamicVertex> vertices, bool dither)
{
	auto& vbo = renderData.Pipeline.Vbo;
	int start = vbo.Count();
	vbo.AddMemoryCopy(vertices);
	if (dither)
	{
		for (int i = start; i < vbo.Count(); i++)
			vbo.Data[i].Dither = 1;
	}
}

void GeometryRenderer::RenderSubsectorWalls(subsector_t* subsector, const Helion::Geometry::Vec2D& pos2D, const Helion::Geometry::Vec2D& prevPos2D)
{
	sector_t* sector = subsector->sector;
	for (uint32_t i = 0; i < subsector->numlines; i++)
	{
		seg_t* edge = &subsector->firstline[i];
		if (edge->linedef == nullptr)
			continue;

		double startX = edge->v1->fX(), startY = edge->v1->fY();
		double endX = edge->v2->fX(), endY = edge->v2->fY();
		double dx = endX - startX;
		double dy = endY - startY;
		bool front = (dx * (pos2D.Y - startY)) - (dy * (pos2D.X - startX)) < 0;
		bool frontPrev = (dx * (prevPos2D.Y - startY)) - (dy * (prevPos2D.X - startX)) < 0;
		if (edge->backsector == nullptr && !front && !frontPrev)
			continue;

		// UZDoom (classic AddLine, r_distance_cull_type): a line beyond the cull distance hides what is behind it and is drawn as
		// a cover wall instead (HWWall::Process with isculled). Only the side facing the view is processed, like the classic clipper.
		if (m_cullDistanceSquared > 0 && IsDistanceCulled(edge))
		{
			if (!front)
				continue;
			if (m_occludeWithWalls)
			{
				m_viewClipper->AddLine(Helion::Geometry::Vec2D(startX, startY), Helion::Geometry::Vec2D(endX, endY));
				m_viewClipperPrev->AddLine(Helion::Geometry::Vec2D(startX, startY), Helion::Geometry::Vec2D(endX, endY));
			}
			if (edge->frontsector->Colormap.FadeColor != 0)
				edge->frontsector->Level->cullcolor = edge->frontsector->Colormap.FadeColor;
			if (edge->sidedef != nullptr && !m_hitLines[edge->linedef->Index()])
				RenderCulledWall(edge, sector);
			continue;
		}

		// UZDoom (plan M5.2): HWWall::Process's portal branches for the visible face of the seg.
		if (m_portalDriver != nullptr && front)
			m_portalDriver->ProcessSeg(edge, edge->frontsector, edge->backsector, subsector);

		// Add segment to the clipper. Bsp segments aren't checked against the clipper because it's more expensive than just rendering the lines
		// Most subsectors are rejecting by the bounding box check before this is reached.
		side_t* side = edge->sidedef;
		if (m_occludeWithWalls && (edge->backsector == nullptr || RenderBlock::IsBlocked(side, edge->frontsector, edge->backsector)))
		{
			m_viewClipper->AddLine(Helion::Geometry::Vec2D(startX, startY), Helion::Geometry::Vec2D(endX, endY));
			m_viewClipperPrev->AddLine(Helion::Geometry::Vec2D(startX, startY), Helion::Geometry::Vec2D(endX, endY));
		}

		int lineIndex = edge->linedef->Index();
		if (m_hitLines[lineIndex])
			continue;

		m_hitLines[lineIndex] = 1;
		RenderSectorLine(edge->linedef, sector, pos2D, prevPos2D);
	}
}

bool GeometryRenderer::IsDistanceCulled(const seg_t* seg) const
{
	DVector2 VPos(m_cullCenter.X, m_cullCenter.Y);
	DVector2 vec1 = (seg->linedef->v1->fPos() - VPos);
	DVector2 vec2 = (seg->linedef->v2->fPos() - VPos);
	double dist1 = vec1.LengthSquared();
	double dist2 = vec2.LengthSquared();
	if ((dist1 > m_cullDistanceSquared) && (dist2 > m_cullDistanceSquared))
	{
		if (vec1.dot(vec2) > 0.0) // Angle subtended by the line at the viewpoint less than 90-degrees
		{
			return true;
		}
		else
		{
			DVector2 linevec = seg->linedef->Delta();
			double linelensq = linevec.LengthSquared();
			if (linelensq > 0.0)
			{
				double projlen = -vec1.dot(linevec) / linelensq;
				DVector2 closest = seg->linedef->v1->fPos() + linevec * projlen;
				if ((VPos - closest).LengthSquared() > m_cullDistanceSquared)
				{
					return true;
				}
			}
		}
	}
	return false;
}

// HWWall::Process for a culled seg: a wall from the floor to the ceiling of the sector, textured with the ceiling flat. Under a sky
// ceiling classic adds the sky above the ceiling (SkyNormal) and nothing in front of the clipped area behind the line, so the whole
// column shows the sky dome: the port draws one sky wall over the full height.
void GeometryRenderer::RenderCulledWall(seg_t* seg, sector_t* sector)
{
	m_hitLines[seg->linedef->Index()] = 1;
	int ceilingHandle = sector->GetTexture(sector_t::ceiling).GetIndex();
	if (IsSkyTexture(ceilingHandle))
	{
		side_t* side = seg->sidedef;
		RenderSector renderSector = GetRenderSector(sector, m_transferHeightsView);
		WallVertices wall;
		WorldTriangulator::HandleOneSided(side, side, renderSector.Floor, renderSector.Ceiling, Helion::Geometry::Vec2F(0, 0), wall,
			renderSector.Floor.Z - MaxSky, renderSector.Ceiling.Z + MaxSky, side == seg->linedef->sidedef[0]);
		SetSkyWallVertices(m_skyWallVertices, wall);
		if (m_buffer)
			m_skyRenderer->AddSectorSky(sector, sector_t::ceiling, m_skyWallVertices.data(), (int)m_skyWallVertices.size());
		return;
	}
	GLLegacyTexture* texture = m_glTextureManager->GetTexture(ceilingHandle);
	if (texture == nullptr || ceilingHandle <= NullCompatibilityTextureIndex)
		return;

	side_t* side = seg->sidedef;
	RenderSector renderSector = GetRenderSector(sector, m_transferHeightsView);
	WallVertices wall = {};
	WorldTriangulator::HandleOneSided(side, side, renderSector.Floor, renderSector.Ceiling, texture->UVInverse, wall,
		WorldTriangulator::NoOverride, WorldTriangulator::NoOverride, side == seg->linedef->sidedef[0]);
	int overrideLightIndex = 0;
	int lightIndex = ::Helion::Render::Renderer::GetLightBufferIndex(side, side_t::mid, sector, overrideLightIndex);
	if (m_culledWall.empty())
		m_culledWall.resize(6);
	SetWallVertices(m_culledWall.data(), wall, GetLightLevelAdd(side), lightIndex, overrideLightIndex, GetWallLightLevel(side, WallLocation::Middle),
		seg->linedef->Index(), WallLocation::Middle, texture->ArrayIndex);
	ApplyWallSlopes(m_culledWall.data(), (int)m_culledWall.size(), &renderSector.Ceiling, &renderSector.Floor);

	if (m_buffer)
	{
		GLLegacyTexture* arrayTexture = m_glTextureManager->GetParentArrayTexture(ceilingHandle);
		auto& renderData = m_worldDataManager->GetRenderData(arrayTexture, GeometryType::Wall, m_glTextureManager->GetBrightmapTexture(ceilingHandle));
		CopyToRenderData(renderData, std::span<const DynamicVertex>(m_culledWall.data(), m_culledWall.size()), false);
	}
}

void GeometryRenderer::RenderSubsector(subsector_t* subsector, const Helion::Geometry::Vec2D& pos2D, const Helion::Geometry::Vec2D& prevPos2D, bool renderSectorFlats)
{
	m_buffer = true;
	sector_t* sector = subsector->sector;
	SetSectorRendering(sector);
	if (m_portalDriver != nullptr)
		m_portalDriver->ProcessSubsector(subsector, sector);

	sector_t* controlSector = sector->GetHeightSec();
	if (controlSector != nullptr)
	{
		RenderSubsectorWalls(subsector, pos2D, prevPos2D);
		if (renderSectorFlats)
			RenderSectorFlats(sector, GetRenderSector(sector, m_transferHeightsView), controlSector);
		return;
	}

	if (renderSectorFlats && m_sector3DTable.HasSectors3D(sector))
	{
		auto& sectors3D = m_sector3DTable.Sectors3D(sector);
		for (size_t i = 0; i < sectors3D.size(); i++)
			RenderSector3D(sectors3D[i].get(), m_viewPosition, m_prevViewPosition);
	}

	RenderSubsectorWalls(subsector, pos2D, prevPos2D);
	if (renderSectorFlats)
		RenderSectorFlats(sector, RenderSector(sector), sector);
}

void GeometryRenderer::RenderSectorGeometry(sector_t* sector, const Helion::Geometry::Vec3D& viewPosition, const Helion::Geometry::Vec3D& prevViewPosition)
{
	m_buffer = true;
	m_viewPosition = viewPosition;
	m_prevViewPosition = prevViewPosition;

	SetSectorRendering(sector);

	Helion::Geometry::Vec2D pos2D(viewPosition.X, viewPosition.Y);
	Helion::Geometry::Vec2D prevPos2D(prevViewPosition.X, prevViewPosition.Y);

	sector_t* controlSector = sector->GetHeightSec();
	if (controlSector != nullptr)
	{
		RenderSectorWalls(sector, pos2D, prevPos2D);
		if (m_renderMode == GeometryRenderMode::All || !AreFlatsStatic(sector))
			RenderSectorFlats(sector, GetRenderSector(sector, m_transferHeightsView), controlSector);
		return;
	}

	RenderSectorWalls(sector, pos2D, prevPos2D);
	if (m_renderMode == GeometryRenderMode::All || !AreFlatsStatic(sector))
		RenderSectorFlats(sector, RenderSector(sector), sector);
}

// UZDoom: Helion's RenderSector of a fake sector with Sector3D != null (3D floors have no fake sector here).
void GeometryRenderer::RenderSector3D(Sector3D* sector3D, const Helion::Geometry::Vec3D& viewPosition, const Helion::Geometry::Vec3D& prevViewPosition)
{
	m_buffer = true;
	m_viewPosition = viewPosition;
	m_prevViewPosition = prevViewPosition;

	// Helion SetSectorRendering(fakeSector): the fake Floor is FakeTop, the fake Ceiling is FakeBottom.
	m_floorChanged = CheckPlaneRenderingChanged(sector3D->FakeTop);
	m_ceilingChanged = CheckPlaneRenderingChanged(sector3D->FakeBottom);

	Helion::Geometry::Vec2D pos2D(viewPosition.X, viewPosition.Y);
	Helion::Geometry::Vec2D prevPos2D(prevViewPosition.X, prevViewPosition.Y);

	if (sector3D->ShouldRenderWalls())
	{
		SetSectorForLineRendering3D(sector3D);
		sector_t* parent = sector3D->ParentSector;
		for (unsigned i = 0; i < parent->Lines.Size(); i++)
		{
			line_t* line = parent->Lines[i];
			// Helion tests the fake line, whose segment is reversed when the parent sector is the front of the real line, so the
			// fake front is always the outside (neighbour) face.
			bool flipped = line->frontsector == parent;
			bool onFront = OnRight(line, pos2D) != flipped;
			// Back sides must be rendered with vanilla rendering for back face sprite clipping to function.
			bool vanillaRenderCheck = false;
			if (m_vanillaRender && sector3D->ControlSide != nullptr)
			{
				GLLegacyTexture* controlTexture = m_glTextureManager->GetTexture(sector3D->ControlSide->GetTexture(side_t::mid).GetIndex());
				vanillaRenderCheck = controlTexture->TransparentPixelCount == 0;
			}
			bool onBothSides = vanillaRenderCheck || onFront != (OnRight(line, prevPos2D) != flipped);
			RenderSectorLine3D(sector3D, (int)i, onFront || onBothSides, !onFront || onBothSides, RenderVertices3DAction());
		}
	}

	if (m_renderMode == GeometryRenderMode::All || !(PlaneIsStatic(sector3D->FakeTop) && PlaneIsStatic(sector3D->FakeBottom)))
		RenderSectorFlats3D(sector3D);
}

void GeometryRenderer::RenderSectorWall(sector_t* sector, line_t* line, const Helion::Geometry::Vec3D& viewPosition, const Helion::Geometry::Vec3D& prevViewPosition)
{
	m_buffer = true;
	m_viewPosition = viewPosition;
	m_prevViewPosition = prevViewPosition;
	SetSectorRendering(sector);
	RenderSectorSideWall(sector, line->sidedef[0], true);
	if (line->sidedef[1] != nullptr)
		RenderSectorSideWall(sector, line->sidedef[1], false);
}

void GeometryRenderer::RenderPolyobjectWall(sector_t* containingSector, side_t* side, const Helion::Geometry::Vec3D& viewPosition, const Helion::Geometry::Vec3D& prevViewPosition)
{
	m_buffer = true;
	m_viewPosition = viewPosition;
	m_prevViewPosition = prevViewPosition;
	SetSectorRendering(containingSector);
	m_polyobjectSector = containingSector;
	RenderSectorSideWall(containingSector, side, side->linedef->sidedef[0] == side);
	m_polyobjectSector = nullptr;
}

void GeometryRenderer::SetSectorRendering(sector_t* sector)
{
	m_floorChanged = CheckPlaneRenderingChanged(sector, sector_t::floor);
	m_ceilingChanged = CheckPlaneRenderingChanged(sector, sector_t::ceiling);

	sector_t* controlSector = sector->GetHeightSec();
	if (controlSector != nullptr)
	{
		bool transferFloorChanged = CheckPlaneRenderingChanged(controlSector, sector_t::floor);
		bool transferCeilingChanged = CheckPlaneRenderingChanged(controlSector, sector_t::ceiling);
		// Transfer heights can swap the rendering from floor to ceiling.
		// If either the floor or ceiling has changed recalculate both to ensure it's correct.
		m_floorChanged = m_floorChanged || m_ceilingChanged || transferFloorChanged || transferCeilingChanged;
		m_ceilingChanged = m_floorChanged || m_ceilingChanged || transferFloorChanged || transferCeilingChanged;
	}
}

void GeometryRenderer::SetInitRender()
{
	SetRenderMode(GeometryRenderMode::Dynamic, TransferHeightView::Middle);
	SetBuffer(false);
	m_floorChanged = true;
	m_ceilingChanged = true;
}

// The set sector is optional for the transfer heights control sector.
// This is so the LastRenderGametick can be set for both the sector and transfer heights sector.
void GeometryRenderer::RenderSectorFlats(sector_t* sectorForSubsectors, const RenderSector& renderSector, sector_t* set)
{
	TArrayView<FSection*> sections = SectionsOfSector(sectorForSubsectors);
	int gametick = Gametick();
	// Helion Sector.LastRenderGametick is never read, it is not kept.
	int floorId = sectorForSubsectors->Index() * 2 + sector_t::floor;
	int ceilingId = sectorForSubsectors->Index() * 2 + sector_t::ceiling;
	int setFloorId = set->Index() * 2 + sector_t::floor;
	int setCeilingId = set->Index() * 2 + sector_t::ceiling;

	if (m_renderMode == GeometryRenderMode::All || sectorForSubsectors->renderDynamic[sector_t::floor] != 0)
	{
		DynamicVertexArray* vertices = nullptr;
		SkyVertexArray* skyVertices = nullptr;
		GLLegacyTexture* texture = nullptr;
		bool success = RenderFlat(sections, renderSector.Floor, RenderPlane::From(sectorForSubsectors, SectorPlaneFace::Floor), true, false, true,
			vertices, skyVertices, texture);

		if (success)
		{
			PlaneCounters(floorId).LastRenderGametick = gametick;
			PlaneCounters(setFloorId).LastRenderGametick = gametick;
		}
	}

	if (m_renderMode == GeometryRenderMode::All || sectorForSubsectors->renderDynamic[sector_t::ceiling] != 0)
	{
		DynamicVertexArray* vertices = nullptr;
		SkyVertexArray* skyVertices = nullptr;
		GLLegacyTexture* texture = nullptr;
		bool success = RenderFlat(sections, renderSector.Ceiling, RenderPlane::From(sectorForSubsectors, SectorPlaneFace::Ceiling), false, false, true,
			vertices, skyVertices, texture);

		if (success)
		{
			PlaneCounters(ceilingId).LastRenderGametick = gametick;
			PlaneCounters(setCeilingId).LastRenderGametick = gametick;
		}
	}
}

// UZDoom: the Sector3D branch of Helion's RenderSectorFlats (sectorForSubsectors.Sector3D != null).
void GeometryRenderer::RenderSectorFlats3D(Sector3D* sector3D)
{
	if (!sector3D->ShouldRenderFlats())
		return;

	// UZDoom: Helion temporarily sets ParentSector.TransferFloorLightSector = ParentSector while rendering the flats. The light
	// sector is passed explicitly (LightTop / LightBottom) and sector_t is never written.
	TArrayView<FSection*> sections = SectionsOfSector(sector3D->ParentSector);
	int gametick = Gametick();

	RenderSector lightTop;
	const RenderSector* lightTopPtr = nullptr;
	if (sector3D->LightTop != nullptr)
	{
		lightTop = RenderSector::From(sector3D->LightTop);
		lightTopPtr = &lightTop;
	}

	RenderSector lightBottom;
	const RenderSector* lightBottomPtr = nullptr;
	if (sector3D->LightBottom != nullptr)
	{
		lightBottom = RenderSector::From(sector3D->LightBottom);
		lightBottomPtr = &lightBottom;
	}

	if (m_renderMode == GeometryRenderMode::All || !PlaneIsStatic(sector3D->FakeTop))
	{
		bool success = false;
		if ((sector3D->RenderPlanes & SectorPlanes::Ceiling) != SectorPlanes::None)
		{
			DynamicVertexArray* vertices = nullptr;
			SkyVertexArray* skyVertices = nullptr;
			GLLegacyTexture* texture = nullptr;
			success |= RenderFlat(sections, sector3D->ControlTop, sector3D->FakeTop, true, false, false, vertices, skyVertices, texture,
				lightTopPtr, true, sector3D->Alpha, sector3D->RenderDataStyle);

			if (sector3D->HasFlipped)
			{
				success |= RenderFlat(sections, sector3D->ControlTop, sector3D->FakeTopFlipped, false, false, false, vertices, skyVertices, texture,
					lightTopPtr, true, sector3D->Alpha, sector3D->RenderDataStyle);
			}
		}

		if (success)
			PlaneCounters(sector3D->FakeTop.Id).LastRenderGametick = gametick;
	}

	if (m_renderMode == GeometryRenderMode::All || !PlaneIsStatic(sector3D->FakeBottom))
	{
		bool success = false;
		if ((sector3D->RenderPlanes & SectorPlanes::Floor) != SectorPlanes::None)
		{
			DynamicVertexArray* vertices = nullptr;
			SkyVertexArray* skyVertices = nullptr;
			GLLegacyTexture* texture = nullptr;
			success |= RenderFlat(sections, sector3D->ControlBottom, sector3D->FakeBottom, false, false, false, vertices, skyVertices, texture,
				lightBottomPtr, true, sector3D->Alpha, sector3D->RenderDataStyle);

			if (sector3D->HasFlipped)
			{
				success |= RenderFlat(sections, sector3D->ControlBottom, sector3D->FakeBottomFlipped, true, false, false, vertices, skyVertices, texture,
					lightBottomPtr, true, sector3D->Alpha, sector3D->RenderDataStyle);
			}
		}

		if (success)
			PlaneCounters(sector3D->FakeBottom.Id).LastRenderGametick = gametick;
	}
}

void GeometryRenderer::Dispose()
{
	if (m_disposed)
		return;

	m_disposed = true;
	ReleaseUnmanagedResources();
}

GeometryRenderer::LoadTextureCounts GeometryRenderer::PreloadAllTextures(FLevelLocals* world)
{
	LoadTextureCounts counts;
	counts.WallTexturesRepeat.reserve(128);
	counts.WallTexturesClamp.reserve(128);
	counts.FlatTextures.reserve(128);

	for (unsigned i = 0; i < world->lines.Size(); i++)
	{
		line_t* line = &world->lines[i];
		bool twoSided = (line->flags & ML_TWOSIDED) != 0;
		if (line->sidedef[0] != nullptr)
			AddSideTextures(counts, line->sidedef[0], twoSided);

		if (line->sidedef[1] == nullptr)
			continue;

		AddSideTextures(counts, line->sidedef[1], twoSided);
	}

	for (unsigned i = 0; i < world->sectors.Size(); i++)
	{
		sector_t* sector = &world->sectors[i];
		counts.FlatTextures.push_back(sector->GetTexture(sector_t::floor).GetIndex());
		counts.FlatTextures.push_back(sector->GetTexture(sector_t::ceiling).GetIndex());
		counts.FlatTextures.push_back(sector->planes[sector_t::floor].skytexture[0].GetIndex());
		counts.FlatTextures.push_back(sector->planes[sector_t::ceiling].skytexture[0].GetIndex());
	}

	// UZDoom: Helion's world.GetPreCacheTextureNames() (map defined precache textures) is covered by hw_precache.cpp.
	SortUnique(counts.FlatTextures);
	SortUnique(counts.WallTexturesRepeat);
	SortUnique(counts.WallTexturesClamp);

	AddLineSwitchTextures(counts);

	// UZDoom: TextureManager.LoadTextureImages is done by the texture manager when the arrays are built (GLTextureArrayBuilder::BuildLevel).
	return counts;
}

void GeometryRenderer::AddLineSwitchTextures(LoadTextureCounts& counts)
{
	// Helion AddLineSwitchTextures: add the opposing texture of every animated switch texture used by a wall.
	// UZDoom: FSwitchDef frames are the switched textures.
	auto addSwitchTextures = [](std::vector<int>& wallTextures)
	{
		std::vector<int> addTextures;
		for (int textureIndex : wallTextures)
		{
			if (textureIndex <= 0)
				continue;

			FSwitchDef* animSwitch = TexAnim.FindSwitch(FSetTextureID(textureIndex));
			if (animSwitch == nullptr)
				continue;

			for (int frame = 0; frame < animSwitch->NumFrames; frame++)
				addTextures.push_back(animSwitch->frames[frame].Texture.GetIndex());
			if (animSwitch->PairDef != nullptr)
				addTextures.push_back(animSwitch->PairDef->PreTexture.GetIndex());
		}

		wallTextures.insert(wallTextures.end(), addTextures.begin(), addTextures.end());
		SortUnique(wallTextures);
	};

	addSwitchTextures(counts.WallTexturesRepeat);
	addSwitchTextures(counts.WallTexturesClamp);
}

void GeometryRenderer::AddSideTextures(LoadTextureCounts& counts, side_t* side, bool twoSided)
{
	counts.WallTexturesRepeat.push_back(side->GetTexture(side_t::bottom).GetIndex());
	counts.WallTexturesRepeat.push_back(side->GetTexture(side_t::top).GetIndex());

	if (twoSided)
		counts.WallTexturesClamp.push_back(side->GetTexture(side_t::mid).GetIndex());
	else
		counts.WallTexturesRepeat.push_back(side->GetTexture(side_t::mid).GetIndex());
}

// UZDoom: Helion's sector3D branch (fake sectors) is RenderSector3D, this is the real sector path.
void GeometryRenderer::RenderSectorWalls(sector_t* sector, const Helion::Geometry::Vec2D& pos2D, const Helion::Geometry::Vec2D& prevPos2D)
{
	for (unsigned i = 0; i < sector->Lines.Size(); i++)
		RenderSectorLine(sector->Lines[i], sector, pos2D, prevPos2D);
}

void GeometryRenderer::RenderSectorLine(line_t* line, sector_t* sector, const Helion::Geometry::Vec2D& pos2D, const Helion::Geometry::Vec2D& prevPos2D)
{
	DVector2 delta = line->Delta();
	double startX = line->v1->fX(), startY = line->v1->fY();
	bool onFront = (delta.X * (pos2D.Y - startY)) - (delta.Y * (pos2D.X - startX)) <= 0;
	bool onBothSides = onFront != ((delta.X * (prevPos2D.Y - startY)) - (delta.Y * (prevPos2D.X - startX)) <= 0);

	side_t* front = line->sidedef[0];
	side_t* back = line->sidedef[1];
	if (back != nullptr)
		CheckFloodFillLine(front, back);

	auto& frontState = RenderStaticState::Side(front);
	// Need to force render for alternative flood fill from the front side.
	if (onFront || onBothSides || frontState.LowerFloodKeys.Key2 > 0 || frontState.UpperFloodKeys.Key2 > 0)
	{
		RenderSectorSideWall(sector, front, true);
	}
	else if (m_vanillaRender && back != nullptr)
	{
		m_renderCoverOnly = true;
		RenderSectorSideWall(sector, front, true);
		m_renderCoverOnly = false;
	}

	if (back == nullptr)
		return;

	auto& backState = RenderStaticState::Side(back);
	// Need to force render for alternative flood fill from the back side.
	if (!onFront || onBothSides || backState.LowerFloodKeys.Key2 > 0 || backState.UpperFloodKeys.Key2 > 0)
	{
		RenderSectorSideWall(sector, back, false);
	}
	else if (m_vanillaRender)
	{
		m_renderCoverOnly = true;
		RenderSectorSideWall(sector, back, false);
		m_renderCoverOnly = false;
	}
}

void GeometryRenderer::CheckFloodFillLine(side_t* front, side_t* back)
{
	if (m_renderMode == GeometryRenderMode::All)
		return;

	int gametick = Gametick();
	if (front->renderDynamic != 0 && m_drawnSides[front->Index()] != WorldStatic::CheckCounter &&
		(CheckSectorRenderingChanged(back->sector, gametick) || CheckSectorRenderingChanged(front->sector, gametick)))
	{
		m_staticCacheGeometryRenderer->CheckForFloodFill(front, back,
			GetRenderSector(front->sector, m_transferHeightsView), GetBackRenderSector(back->sector, m_transferHeightsView), true);
	}

	if (back->renderDynamic != 0 && m_drawnSides[back->Index()] != WorldStatic::CheckCounter &&
		(CheckSectorRenderingChanged(front->sector, gametick) || CheckSectorRenderingChanged(back->sector, gametick)))
	{
		m_staticCacheGeometryRenderer->CheckForFloodFill(back, front,
			GetRenderSector(back->sector, m_transferHeightsView), GetBackRenderSector(front->sector, m_transferHeightsView), false);
	}
}

void GeometryRenderer::RenderSectorSideWall(sector_t* sector, side_t* side, bool onFrontSide)
{
	if (m_drawnSides[side->Index()] == WorldStatic::CheckCounter)
		return;

	m_drawnSides[side->Index()] = WorldStatic::CheckCounter;
	if (TextureTransparency && SideLineAlpha(side) < 1)
		RenderAlphaSide(side, onFrontSide);

	bool transferHeights = false;
	// Transfer heights has to be drawn by the transfer heights sector
	sector_t* sideControlSector = side->sector->GetHeightSec();
	if (sideControlSector != nullptr && (sector->GetHeightSec() == nullptr || sector->GetHeightSec() != sideControlSector))
	{
		SetSectorRendering(side->sector);
		transferHeights = true;
	}

	if (m_renderMode == GeometryRenderMode::All || side->renderDynamic != 0)
		RenderSide(side, onFrontSide);

	// Restore to original sector
	if (transferHeights)
		SetSectorRendering(sector);
}

void GeometryRenderer::RenderAlphaSide(side_t* side, bool isFrontSide)
{
	if (side->linedef->sidedef[1] == nullptr || side->GetTexture(side_t::mid).GetIndex() <= NoTextureIndex)
		return;

	side_t* otherSide = PartnerSideOf(side);
	SideRenderCounters& counters = SideCounters(side);
	m_sectorChangedLine = CheckSectorRenderingChanged(otherSide->sector, counters.LastRenderGametickAlpha) ||
		CheckSectorRenderingChanged(side->sector, counters.LastRenderGametickAlpha);

	CheckInvalidatedSideCounter(counters.AlphaInvalidatedCount, m_invalidatedCounter);

	RenderSector facingSector = GetRenderSector(side->sector, m_transferHeightsView);
	RenderSector otherSector = GetBackRenderSector(otherSide->sector, m_transferHeightsView);
	DynamicVertexArray* midTexVertices = nullptr;
	RenderTwoSidedMiddle(side, otherSide, facingSector, otherSector, isFrontSide, midTexVertices);
	counters.LastRenderGametickAlpha = Gametick();

	if (midTexVertices != nullptr && m_vanillaRender)
	{
		SideTexture visibility = GetSideVisibility(side, otherSide, facingSector, otherSector);
		if ((visibility & SideTexture::Upper) == SideTexture::None || (visibility & SideTexture::Lower) == SideTexture::None)
		{
			bool bufferCoverWall = m_worldDataManager->BufferCoverWalls;
			SetBufferCoverWall(true);
			// Need to copy since the vertices may be part of member variable cache
			size_t count = std::min<size_t>(midTexVertices->size(), 6);
			for (size_t i = 0; i < count; i++)
				m_coverWallVertices[i] = (*midTexVertices)[i];
			RenderMidTexCoverWalls(side, facingSector, otherSector, std::span<DynamicVertex>(&m_coverWallVertices[0], 6), visibility, m_renderCoverWallAction);
			SetBufferCoverWall(bufferCoverWall);
		}
	}
}

void GeometryRenderer::RenderSide(side_t* side, bool isFrontSide)
{
	auto& state = RenderStaticState::Side(side);
	if (state.FloorFloodKey > 0)
		m_portals->UpdateFloodFillPlane(side, GetRenderSector(side->sector, m_transferHeightsView), SectorPlanes::Floor, SectorPlaneFace::Floor, isFrontSide);
	if (state.CeilingFloodKey > 0)
		m_portals->UpdateFloodFillPlane(side, GetRenderSector(side->sector, m_transferHeightsView), SectorPlanes::Ceiling, SectorPlaneFace::Ceiling, isFrontSide);

	if ((side->linedef->flags & ML_TWOSIDED) != 0 && side->linedef->sidedef[1] != nullptr)
	{
		RenderTwoSided(side, isFrontSide);
	}
	else if (m_renderMode == GeometryRenderMode::All || side->renderDynamic != 0)
	{
		if (m_sector3DTable.HasSectors3D(side->sector))
		{
			RenderSector sector(side->sector);
			auto& planes3D = m_sector3DTable.SectorPlanes3D(side->sector);
			RenderWallSlices3D(side, WallLocation::Middle, isFrontSide, side, sector, sector, std::span<SectorPlane3D>(planes3D), m_renderOneSidedSliceFunc);
		}
		else
		{
			DynamicVertexArray* vertices = nullptr;
			SkyVertexArray* skyVertices = nullptr;
			GLLegacyTexture* texture = nullptr;
			RenderOneSided(side, isFrontSide, vertices, skyVertices, texture);
		}
	}
}

void GeometryRenderer::RenderOneSided(side_t* side, bool isFront, DynamicVertexArray*& vertices, SkyVertexArray*& skyVertices, GLLegacyTexture*& texture,
	const RenderSector* renderSector, const RenderSector* lightLevelSector, side_t* offsetSide, bool renderSkySide, bool allowAlpha,
	Data::RenderDataStyle style, GeometryType baseType)
{
	skyVertices = nullptr;
	SetRenderOneSided(side, renderSector);

	SideCounters(side).LastRenderGametick = Gametick();

	CheckInvalidatedSideCounter(SideInvalidatedCount(side, WallLocation::Middle), m_invalidatedCounter);

	WallVertices wall = {};
	int middleHandle = side->GetTexture(side_t::mid).GetIndex();
	texture = m_glTextureManager->GetTexture(middleHandle);
	GLLegacyTexture* brightmapTexture = m_glTextureManager->GetBrightmapTexture(middleHandle);
	DynamicVertexArray* data = GetCachedSide(m_vertexLookup, side);

	RenderSector localSector;
	const RenderSector* sectorPtr = renderSector;
	if (sectorPtr == nullptr)
	{
		localSector = GetRenderSector(m_polyobjectSector != nullptr ? m_polyobjectSector : side->sector, m_transferHeightsView);
		sectorPtr = &localSector;
	}
	if (lightLevelSector == nullptr)
		lightLevelSector = sectorPtr;

	const RenderPlane* floor = &sectorPtr->Floor;
	const RenderPlane* ceiling = &sectorPtr->Ceiling;
	bool slopePlanes = true;
	if (renderSkySide)
	{
		// UZDoom: the classic renderer draws nothing for a one sided wall without a texture, so under a sky ceiling the sky
		// (the sky drawn before the scene, see RenderSkies) shows there (Pirates! MAP58 open sea). Helion draws the null
		// texture, which comes out black in the opaque pass. Make the whole wall sky instead, it still blocks the geometry behind.
		const bool untexturedSkyWall = middleHandle <= NullCompatibilityTextureIndex && !Portals::PortalDriver::SideReplacedByPortal(side);
		RenderSkySide(side, *sectorPtr, nullptr, texture, isFront, skyVertices, untexturedSkyWall);
		if (untexturedSkyWall && skyVertices != nullptr)
		{
			vertices = nullptr;
			return;
		}

		// One-sided walls without a texture would HOM and stop BSP traversal. Draw a black texture to block rendering.
		if (skyVertices == nullptr && middleHandle <= NullCompatibilityTextureIndex)
		{
			constexpr double MaxTextureHeight = 16384;
			m_fakeFloor.Z = floor->Z - MaxTextureHeight;
			m_fakeFloor.PrevZ = floor->PrevZ - MaxTextureHeight;
			m_fakeCeiling.Z = ceiling->Z + MaxTextureHeight;
			m_fakeCeiling.PrevZ = ceiling->PrevZ + MaxTextureHeight;
			floor = &m_fakeFloor;
			ceiling = &m_fakeCeiling;
			texture = m_glTextureManager->BlackTexture;
			brightmapTexture = nullptr;
			slopePlanes = false;
		}
	}

	// UZDoom (plan M5.2): a mirror, one sided line portal, horizon or skybox line shows the portal instead of the wall
	// (HWWall::Process returns after PutPortal).
	if (Portals::PortalDriver::SideReplacedByPortal(side))
	{
		vertices = nullptr;
		return;
	}

	if (SideOffsetChanged(side) || m_sectorChangedLine || data == nullptr || m_polyobjectSector != nullptr)
	{
		int overrideLightIndex = 0;
		// UZDoom: slice sectors (3D floors) have no sector_t, the light then comes from the side's sector.
		int lightIndex = ::Helion::Render::Renderer::GetLightBufferIndex(side, side_t::mid,
			lightLevelSector->Sector != nullptr ? lightLevelSector->Sector : (m_polyobjectSector != nullptr ? m_polyobjectSector : side->sector), overrideLightIndex);
		int addAlpha = allowAlpha ? 0 : 1;
		WorldTriangulator::HandleOneSided(side, offsetSide != nullptr ? offsetSide : side, *floor, *ceiling, texture->UVInverse, wall,
			WorldTriangulator::NoOverride, WorldTriangulator::NoOverride, isFront);
		float alpha = (float)(side->HasAlpha() ? side->alpha : side->linedef->alpha);
		int mapId = side->linedef->Index();
		if (data == nullptr)
		{
			std::unique_ptr<DynamicVertexArray> created = GetWallVertices(wall, GetLightLevelAdd(side), lightIndex, overrideLightIndex,
				GetWallLightLevel(side, WallLocation::Middle), mapId, WallLocation::Middle, texture->ArrayIndex, alpha, addAlpha);
			data = created.get();
			SetCachedSide(m_vertexLookup, side, std::move(created));
		}
		else
		{
			SetWallVertices(data->data(), wall, GetLightLevelAdd(side), lightIndex, overrideLightIndex, GetWallLightLevel(side, WallLocation::Middle),
				mapId, WallLocation::Middle, texture->ArrayIndex, alpha, addAlpha);
		}

		if (renderSector == nullptr)
			SetWallLightmap(data->data(), side, WallLocation::Middle, false);

		// UZDoom: slopes. Slice sectors (renderSector != null) carry plain heights, the sentinel planes of the HOM block have no slope;
		// a one sided slice takes the sloped planes it is cut by (m_sliceSlopeTop/Bottom, set by RenderWallSlices3D).
		if (slopePlanes && renderSector == nullptr)
			ApplyWallSlopes(data->data(), (int)data->size(), ceiling, floor);
		else if (slopePlanes && (m_sliceSlopeTop != nullptr || m_sliceSlopeBottom != nullptr))
			ApplySliceSlopes(data->data(), (int)data->size(), m_sliceSlopeTop, m_sliceSlopeBottom);
	}

	if (m_buffer)
	{
		GeometryType geometryType = GetGeometryType(style, baseType);
		GLLegacyTexture* arrayTexture = m_glTextureManager->GetParentArrayTexture(middleHandle);
		auto& renderData = m_worldDataManager->GetRenderData(arrayTexture, geometryType, brightmapTexture);
		CopyToRenderData(renderData, std::span<const DynamicVertex>(data->data(), data->size()), m_dither && DitherTransparency::SideDithered(side, side_t::mid));
		if (m_vanillaRender && baseType == GeometryType::Wall && style == RenderDataStyle::Normal)
			m_worldDataManager->AddCoverWallVertices(side, std::span<DynamicVertex>(data->data(), data->size()), WallLocation::Middle, true);
	}

	vertices = data;
}

GeometryType GeometryRenderer::GetGeometryType(RenderDataStyle style, GeometryType baseType)
{
	if (style != RenderDataStyle::Normal)
		return ToGeometryType(style);
	return baseType;
}

int GeometryRenderer::GetLightLevelAdd(side_t* side)
{
	// RenderContrastMode: 0 = Off, 1 = Vanilla, 2 = Smooth (r_fakecontrast).
	if (m_contrastMode == 0)
		return 0;

	if ((side->Flags & WALLF_NOFAKECONTRAST) != 0)
		return 0;

	// UZDoom: side_t::GetFakeContrastRel, which takes the level's horizwalllight / vertwalllight (0 with evenlighting and in Hexen
	// format maps) and LEVEL2_SMOOTHLIGHTING instead of Helion's fixed +-16.
	return side->GetFakeContrastRel((m_world->flags2 & LEVEL2_SMOOTHLIGHTING) || (side->Flags & WALLF_SMOOTHLIGHTING) || m_contrastMode == 2);
}

void GeometryRenderer::SetRenderOneSided(side_t* side, const RenderSector* renderSector)
{
	sector_t* sector = side->sector;
	int lastRender = SideCounters(side).LastRenderGametick;
	bool sectorChanged;
	if (renderSector != nullptr)
	{
		// UZDoom: a render sector can be a slice sector or a transfer heights view, its planes carry the heights that changed.
		sectorChanged = CheckSectorRenderingChanged((unsigned)renderSector->Id < m_world->sectors.Size() ? &m_world->sectors[renderSector->Id] : sector, lastRender) ||
			renderSector->Floor.PrevZ != renderSector->Floor.Z || renderSector->Ceiling.PrevZ != renderSector->Ceiling.Z;
	}
	else
	{
		sectorChanged = CheckSectorRenderingChanged(sector, lastRender);
	}

	m_sectorChangedLine = CheckSideRenderingChanged(side) || sectorChanged;
}

void GeometryRenderer::SetRenderTwoSided(side_t* facingSide, side_t* otherSide)
{
	int lastRender = SideCounters(facingSide).LastRenderGametick;
	m_sectorChangedLine = CheckSideRenderingChanged(facingSide) ||
		CheckSectorRenderingChanged(otherSide->sector, lastRender) ||
		CheckSectorRenderingChanged(facingSide->sector, lastRender);
}

void GeometryRenderer::SetRenderFloor(const RenderPlane& floor)
{
	RenderSector sector = GetRenderSector(RealSectorOfPlane(floor), TransferHeightView::Middle);
	m_floorChanged = CheckPlaneRenderingChanged(sector.Floor);
}

void GeometryRenderer::SetRenderCeiling(const RenderPlane& ceiling)
{
	RenderSector sector = GetRenderSector(RealSectorOfPlane(ceiling), TransferHeightView::Middle);
	m_ceilingChanged = CheckPlaneRenderingChanged(sector.Ceiling);
}

void GeometryRenderer::RenderTwoSided(side_t* facingSide, bool isFrontSide)
{
	side_t* otherSide = PartnerSideOf(facingSide);
	RenderSector facingSector = GetRenderSector(facingSide->sector, m_transferHeightsView);
	RenderSector otherSector = GetBackRenderSector(otherSide->sector, m_transferHeightsView);

	SetRenderTwoSided(facingSide, otherSide);

	// UZDoom (HWWall::Process): a two sided polyobject line keeps the sectors of its sides (where the polyobject was built) for its
	// heights and texture alignment, but is lit by the sector the polyobject is in. Its vertices move, so its cached vertices are
	// refreshed every time.
	RenderSector polyobjectLightSector;
	const RenderSector* lightLevelSector = nullptr;
	if (m_polyobjectSector != nullptr)
	{
		polyobjectLightSector = GetRenderSector(m_polyobjectSector, m_transferHeightsView);
		lightLevelSector = &polyobjectLightSector;
		m_sectorChangedLine = true;
	}

	// Don't set the game tick if rendering cover walls. This will prevent lines from rendering when the camera goes from back side to front.
	if (!m_renderCoverOnly)
		SideCounters(facingSide).LastRenderGametick = Gametick();

	SideTexture visibility = GetSideVisibility(facingSide, otherSide, facingSector, otherSector);
	bool renderSlices3D = m_sector3DTable.HasSectors3D(facingSide->sector);

	if ((visibility & SideTexture::Lower) != SideTexture::None)
	{
		if (renderSlices3D)
		{
			RenderWallSlices3D(facingSide, WallLocation::Lower, isFrontSide, otherSide, facingSector, otherSector,
				std::span<SectorPlane3D>(m_sector3DTable.SectorPlanes3D(facingSide->sector)), m_renderTwoSidedLowerSliceFunc, nullptr, false);
		}
		else
		{
			DynamicVertexArray* vertices = nullptr;
			SkyVertexArray* skyVertices = nullptr;
			SkyVertexArray* skyVertices2 = nullptr;
			RenderTwoSidedLower(facingSide, otherSide, facingSector, otherSector, isFrontSide, vertices, skyVertices, skyVertices2, lightLevelSector);
		}
	}

	if ((visibility & SideTexture::Upper) != SideTexture::None)
	{
		if (renderSlices3D)
		{
			RenderWallSlices3D(facingSide, WallLocation::Upper, isFrontSide, otherSide, facingSector, otherSector,
				std::span<SectorPlane3D>(m_sector3DTable.SectorPlanes3D(facingSide->sector)), m_renderTwoSidedUpperSliceFunc);
		}
		else
		{
			DynamicVertexArray* vertices = nullptr;
			SkyVertexArray* skyVertices = nullptr;
			SkyVertexArray* skyVertices2 = nullptr;
			RenderTwoSidedUpper(facingSide, otherSide, facingSector, otherSector, isFrontSide, vertices, skyVertices, skyVertices2, lightLevelSector);
		}
	}

	if ((visibility & SideTexture::Middle) != SideTexture::None)
	{
		std::span<DynamicVertex> vertices;
		if (renderSlices3D)
		{
			RenderWallSliceResult result = RenderWallSlices3D(facingSide, WallLocation::Middle, isFrontSide, otherSide, facingSector, otherSector,
				std::span<SectorPlane3D>(m_sector3DTable.SectorPlanes3D(facingSide->sector)), m_renderTwoSidedMiddleSliceFunc);
			vertices = result.Vertices;
		}
		else
		{
			DynamicVertexArray* midTexVertices = nullptr;
			RenderTwoSidedMiddle(facingSide, otherSide, facingSector, otherSector, isFrontSide, midTexVertices, lightLevelSector);
			if (midTexVertices != nullptr)
				vertices = std::span<DynamicVertex>(*midTexVertices);
		}

		if (!vertices.empty() && m_vanillaRender && m_buffer)
			RenderMidTexCoverWalls(facingSide, facingSector, otherSector, vertices, visibility, m_renderCoverWallAction);
	}

	if (ShouldRenderFogBarrier(facingSide, otherSide))
	{
		DynamicVertexArray* vertices = nullptr;
		RenderFogBarrier(facingSide, otherSide, facingSector, otherSector, isFrontSide, vertices);
	}
}

SideTexture GeometryRenderer::GetSideVisibility(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector)
{
	SideTexture visibility = SideTexture::None;
	bool dynamic = m_renderMode == GeometryRenderMode::All || facingSide->renderDynamic != 0;
	bool transferHeights;
	if (dynamic && IsLowerVisibleWithTransferHeights(facingSide, otherSide, facingSector, otherSector, transferHeights))
		visibility |= SideTexture::Lower;
	bool skyHack;
	if (dynamic && UpperIsVisibleOrFlood(facingSide, otherSide, facingSector, otherSector, skyHack))
		visibility |= SideTexture::Upper;
	if (dynamic && (!TextureTransparency || SideLineAlpha(facingSide) >= 1) && facingSide->GetTexture(side_t::mid).GetIndex() > NoTextureIndex)
		visibility |= SideTexture::Middle;
	return visibility;
}

void GeometryRenderer::RenderMidTexCoverWalls(side_t* side, const RenderSector& facingSector, const RenderSector& otherSector,
	std::span<DynamicVertex> midTexVertices, SideTexture visibleTextures, const RenderCoverWallAction& render)
{
	// Check if this middle wall should clip to floor/ceiling.
	// If not then project the cover walls from the floor/ceiling to block sprites from rendering to match software behavior.
	MidTexOpening opening, prevOpening;
	SectorPlanes clipPlanes = GetMidTexClipPlanes(side, facingSector, otherSector, opening, prevOpening);
	side_t* partnerSide = PartnerSideOf(side);
	SideTexture sideFlood = RenderStaticState::Side(side).FloodTextures;
	SideTexture partnerFlood = partnerSide == nullptr ? SideTexture::None : RenderStaticState::Side(partnerSide).FloodTextures;
	if (
		((visibleTextures & SideTexture::Lower) == SideTexture::None || (sideFlood & SideTexture::Lower) != SideTexture::None) &&
		(partnerSide == nullptr || (partnerFlood & SideTexture::Lower) == SideTexture::None) &&
		(clipPlanes & SectorPlanes::Floor) != SectorPlanes::None)
	{
		float bottomZ = (float)opening.MinBottomZ;
		float prevBottomZ = (float)prevOpening.MinBottomZ;
		for (size_t i = 0; i < m_wallVertices.size(); i++)
		{
			m_wallVertices[i] = midTexVertices[i];
			m_wallVertices[i].Z = bottomZ + WorldStatic::CoverWallOffset;
			m_wallVertices[i].PrevZ = prevBottomZ + WorldStatic::CoverWallOffset;
		}

		CoverWallUtil::SetCoverWallVertices(side, m_wallVertices.data(), 0, WallLocation::Lower);
		render(side, std::span<DynamicVertex>(m_wallVertices.data(), m_wallVertices.size()), WallLocation::Lower, true);
	}

	if (
		((visibleTextures & SideTexture::Upper) == SideTexture::None || (sideFlood & SideTexture::Upper) != SideTexture::None) &&
		(partnerSide == nullptr || (partnerFlood & SideTexture::Upper) == SideTexture::None) &&
		(clipPlanes & SectorPlanes::Ceiling) != SectorPlanes::None)
	{
		float topZ = (float)opening.MaxTopZ;
		float prevTopZ = (float)prevOpening.MaxTopZ;
		for (size_t i = 0; i < m_wallVertices.size(); i++)
		{
			m_wallVertices[i] = midTexVertices[i];
			m_wallVertices[i].Z = topZ - WorldStatic::CoverWallOffset;
			m_wallVertices[i].PrevZ = prevTopZ - WorldStatic::CoverWallOffset;
		}

		CoverWallUtil::SetCoverWallVertices(side, m_wallVertices.data(), 0, WallLocation::Upper);
		render(side, std::span<DynamicVertex>(m_wallVertices.data(), m_wallVertices.size()), WallLocation::Upper, true);
	}
}

GeometryRenderer::SectorPlanes GeometryRenderer::GetMidTexClipPlanes(side_t* side, const RenderSector& facingSector, const RenderSector& otherSector,
	MidTexOpening& opening, MidTexOpening& prevOpening)
{
	opening = GetMidTexOpening(side, facingSector, otherSector, false);
	prevOpening = GetMidTexOpening(side, facingSector, otherSector, true);
	return GetTwoSidedMiddleClipPlanesVanilla(facingSector, otherSector);
}

// Trick with putting monsters in a lower sector and setting the transfer heights to the surrounding floor.
// Need to render normal lower textures to block the sprites in this case. Only matters with vanilla render.
bool GeometryRenderer::IsLowerVisibleWithTransferHeights(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector,
	bool& transferHeights)
{
	transferHeights = false;
	if (LowerIsVisible(facingSide, facingSector, otherSector))
		return true;

	bool facingTransfer = facingSide->sector->GetHeightSec() != nullptr;
	bool otherTransfer = otherSide->sector->GetHeightSec() != nullptr;
	if (m_vanillaRender && (facingTransfer || otherTransfer) && (!facingTransfer || !otherTransfer))
	{
		transferHeights = true;
		return LowerIsVisible(facingSide, RenderSector(facingSide->sector), RenderSector(otherSide->sector));
	}

	return false;
}

bool GeometryRenderer::LowerIsVisible(side_t* facingSide, const RenderSector& facingSector, const RenderSector& otherSector)
{
	return facingSector.Floor.Z < otherSector.Floor.Z || facingSector.Floor.PrevZ < otherSector.Floor.PrevZ ||
		RenderStaticState::Side(facingSide).LowerFloodKeys.Key1 > 0;
}

bool GeometryRenderer::UpperIsVisible(side_t* facingSide, const RenderSector& facingSector, const RenderSector& otherSector)
{
	return facingSector.Ceiling.Z > otherSector.Ceiling.Z || facingSector.Ceiling.PrevZ > otherSector.Ceiling.PrevZ ||
		RenderStaticState::Side(facingSide).UpperFloodKeys.Key1 > 0;
}

bool GeometryRenderer::UpperIsVisibleOrFlood(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool& skyHack)
{
	bool isSky = IsSkyTexture(facingSector.Ceiling.TextureHandle);
	bool isOtherSky = IsSkyTexture(otherSector.Ceiling.TextureHandle);

	bool upperVisible = UpperOrSkySideIsVisible(facingSide, facingSector, otherSector, skyHack);
	if (!upperVisible && !skyHack && !isOtherSky && isSky)
		return true;

	return upperVisible;
}

bool GeometryRenderer::UpperOrSkySideIsVisible(side_t* facingSide, const RenderSector& facingSector, const RenderSector& otherSector, bool& skyHack)
{
	skyHack = false;
	double facingZ = facingSector.Ceiling.Z;
	double otherZ = otherSector.Ceiling.Z;
	double prevFacingZ = facingSector.Ceiling.PrevZ;
	double prevOtherZ = otherSector.Ceiling.PrevZ;
	bool isFacingSky = IsSkyTexture(facingSector.Ceiling.TextureHandle);
	bool isOtherSky = IsSkyTexture(otherSector.Ceiling.TextureHandle);

	if (isFacingSky && isOtherSky)
	{
		// The sky is only drawn if there is no opening height
		// Otherwise ignore this line for sky effects
		skyHack = GetOpeningHeight(facingSide->linedef) <= 0 && facingZ != otherZ;
		return skyHack;
	}

	bool upperVisible = facingZ > otherZ || prevFacingZ > prevOtherZ;
	// Return true if the upper is not visible so DrawTwoSidedUpper can attempt to draw sky hacks
	if (isFacingSky)
	{
		if ((RenderStaticState::Side(facingSide).FloodTextures & SideTexture::Upper) != SideTexture::None)
			return true;

		if (facingSide->GetTexture(side_t::top).GetIndex() <= NoTextureIndex)
		{
			skyHack = facingZ <= otherZ || prevFacingZ <= prevOtherZ;
			return skyHack;
		}

		// Need to draw sky upper if other sector is not sky.
		skyHack = !isOtherSky;
		return skyHack;
	}

	return upperVisible;
}

bool GeometryRenderer::ShouldRenderFogBarrier(side_t* side, side_t* otherSide)
{
	// Only render if this side has fog and the other does not. First light level is skipped (248) and at least one side must not be sky.
	sector_t* sector = side->sector;
	sector_t* otherSector = otherSide->sector;
	if (!sector->Colormap.FadeColor.isBlack() && otherSector->Colormap.FadeColor.isBlack() && sector->GetLightLevel() <= 248)
	{
		return !IsSkyTexture(sector->GetTexture(sector_t::ceiling).GetIndex()) ||
			!IsSkyTexture(otherSector->GetTexture(sector_t::ceiling).GetIndex());
	}

	return false;
}

void GeometryRenderer::RefreshSector3D()
{
	m_sector3DTable.UpdateAll();
}

// ---- Helion GeometryRenderer.cs lines 1200-1780
// NEEDS (includes): "g_levellocals.h", "r_defs.h", "r_sky.h" (skyflatnum), "doomdata.h" (ML_*), "texturemanager.h", "gametexture.h", "textureid.h",
//   "Renderer.h" (Helion::Render::Renderer::GetLightBufferIndex), "RenderBlock.h", "HelionCVars.h",
//   "Common/Shared/World/WorldTriangulator.h", "World/WorldStatic.h", "World/Static/RenderStaticState.h",
//   "OpenGL/Renderers/Legacy/World/Geometry/Static/LightBufferType.h", "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalRenderer.h" (FloodSet in namespace Portals)
// NEEDS (usings): using ::Helion::Render::RenderBlock; using ::Helion::Render::OpenGL::Shared::World::WorldTriangulator;
//   using ::Helion::World::WorldStatic; using ::Helion::World::Static::RenderStaticState; using Static::LightBufferType;
//   using ::Helion::World::Geometry::Sectors::SectorPlanes; using ::Helion::World::Geometry::Sectors::SectorPlaneFace;
//   using SideTexture = ::Helion::World::Static::SideTexture (class alias already in GeometryRenderer; file scope needs ::Helion::World::Static::operator| & via ADL)
// NEEDS (members Core1 defines): SideOffsetChanged(const side_t*), SideInvalidatedCount(const side_t*, WallLocation), GetCachedSide/SetCachedSide (ownership + NoCache contract),
//   GetLightLevelAdd, GetWallLightLevel, CheckInvalidatedSideCounter, CreateSkyWallVertices/SetSkyWallVertices, LowerIsVisible/UpperIsVisible/UpperOrSkySideIsVisible,
//   m_skyRenderer->AddSectorSky(sector_t*, int plane, const SkyGeometryVertex*, int), Portals().UpdateStaticFloodFillSide(...)->Portals::FloodSet
// NEEDS (Core3): GetWallVertices(...)->std::unique_ptr<DynamicVertexArray>, SetWallVertices(DynamicVertex*, ...), ApplyWallSlopes

namespace
{

// UZDoom: Helion Constants.NoTextureIndex / NullCompatibilityTextureIndex. A tier without a valid texture ("-" or missing).
inline bool PartB_NoTexture(FTextureID id)
{
	return !id.isValid();
}

// UZDoom: Helion TextureManager.IsSkyTexture(handle).
inline bool PartB_IsSky(int textureHandle)
{
	return textureHandle == skyflatnum.GetIndex();
}

// UZDoom: the handle (FTextureID::GetIndex()) of the texture to draw; animations are resolved at draw time by GLTextureManager::GetTexture(int).
inline int PartB_Handle(FTextureID id)
{
	return id.GetIndex();
}

// Helion Side.PartnerSide
inline side_t* PartB_Partner(const side_t* side)
{
	line_t* line = side->linedef;
	return line->sidedef[0] == side ? line->sidedef[1] : line->sidedef[0];
}

inline bool PartB_IsBlocked(side_t* side, const Helion::World::Geometry::Sectors::RenderSector& facingSector,
	const Helion::World::Geometry::Sectors::RenderSector& otherSector)
{
	return ::Helion::Render::RenderBlock::IsBlocked(side, facingSector.Floor, otherSector.Floor, facingSector.Ceiling, otherSector.Ceiling);
}

// Helion StaticDataApplier.ShouldFloodLower
inline bool PartB_ShouldFloodLower(side_t* facingSide, const Helion::World::Geometry::Sectors::RenderSector& facingSector,
	const Helion::World::Geometry::Sectors::RenderSector& otherSector)
{
	return PartB_NoTexture(facingSide->GetTexture(side_t::bottom)) &&
		(facingSector.Floor.Z < otherSector.Floor.Z || facingSector.Floor.PrevZ < otherSector.Floor.PrevZ);
}

// UZDoom (hw_sky.cpp): GLSector_NoSkyDraw / SECMF_NOSKYWALLS on the other sector or ML_NOSKYWALLS on the line suppress the sky walls.
inline bool SkyWallsDisabled(side_t* facingSide, const Helion::World::Geometry::Sectors::RenderSector& otherSector)
{
	const sector_t* other = otherSector.Sector != nullptr ? otherSector.Sector : PartB_Partner(facingSide)->sector;
	return other->special == GLSector_NoSkyDraw || (other->MoreFlags & SECMF_NOSKYWALLS) != 0 || (facingSide->linedef->flags & ML_NOSKYWALLS) != 0;
}

}

void GeometryRenderer::RenderTwoSidedLower(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector,
	bool isFrontSide, DynamicVertexArray*& vertices, SkyVertexArray*& skyVertices, SkyVertexArray*& skyVertices2, const RenderSector* lightLevelSector,
	bool renderSkySide)
{
	vertices = nullptr;
	skyVertices = nullptr;
	skyVertices2 = nullptr;

	const FTextureID lowerTexture = facingSide->GetTexture(side_t::bottom);
	const bool noLowerTexture = PartB_NoTexture(lowerTexture);
	bool isSky = PartB_IsSky(otherSector.Floor.TextureHandle) && noLowerTexture && otherSide->sector->GetHeightSec() == nullptr;

	if (m_vanillaRender && ((RenderStaticState::Side(facingSide).FloodTextures & SideTexture::Lower) == SideTexture::None || isSky))
		RenderCoverWall(WallLocation::Lower, facingSide, facingSector, otherSector, isFrontSide);

	if (m_renderCoverOnly)
		return;

	WallVertices wall;
	bool skyRender = isSky && PartB_IsSky(otherSector.Floor.TextureHandle);

	if (!TwoSidedLowerFlood(facingSide, otherSide, facingSector, otherSector, isFrontSide))
		return;

	if (noLowerTexture && !skyRender)
		return;

	CheckInvalidatedSideCounter(SideInvalidatedCount(facingSide, WallLocation::Lower), m_invalidatedCounter);

	const int lowerHandle = PartB_Handle(lowerTexture);
	GLLegacyTexture* texture = m_glTextureManager->GetTexture(lowerHandle);
	GLLegacyTexture* brightmapTexture = m_glTextureManager->GetBrightmapTexture(lowerHandle);

	RenderPlane top = otherSector.Floor;
	RenderPlane bottom = facingSector.Floor;
	if (lightLevelSector == nullptr)
		lightLevelSector = &facingSector;

	if (renderSkySide && PartB_IsBlocked(facingSide, facingSector, otherSector))
		RenderSkySide(facingSide, facingSector, &otherSector, texture, isFrontSide, skyVertices2);

	// Helion Side.NoCache sides (fog/3D slice sentinels) never share the per side lookups.
	const bool noCache = IsFogSide(facingSide) || facingSide == m_fakeSide.get();

	if (isSky)
	{
		SkyVertexArray* data = noCache ? m_noCacheSkyLower.get() : m_skyWallVertexLowerLookup[facingSide->Index()].get();
		if (noCache)
			data = nullptr;

		if (SideOffsetChanged(facingSide) || m_sectorChangedLine || data == nullptr)
		{
			WorldTriangulator::HandleTwoSidedLower(facingSide, top, bottom, texture->UVInverse, isFrontSide, wall);
			if (data == nullptr)
			{
				std::unique_ptr<SkyVertexArray> created = CreateSkyWallVertices(wall);
				data = created.get();
				if (noCache)
					m_noCacheSkyLower = std::move(created);
				else
					m_skyWallVertexLowerLookup[facingSide->Index()] = std::move(created);
			}
			else
			{
				SetSkyWallVertices(*data, wall);
			}
		}

		if (m_buffer)
			m_skyRenderer->AddSectorSky(otherSide->sector, sector_t::floor, data->data(), (int)data->size());
		vertices = nullptr;
		skyVertices = data;
	}
	else
	{
		DynamicVertexArray* data = GetCachedSide(m_vertexLowerLookup, facingSide);

		if (SideOffsetChanged(facingSide) || m_sectorChangedLine || data == nullptr)
		{
			int overrideLightIndex = 0;
			int lightIndex = ::Helion::Render::Renderer::GetLightBufferIndex(facingSide, side_t::bottom, lightLevelSector->Sector, overrideLightIndex);
			// This lower would clip into the upper texture. Pick the upper as the priority and stop at the ceiling.
			if (top.Z > otherSector.Ceiling.Z && !PartB_IsSky(otherSector.Ceiling.TextureHandle))
				top = otherSector.Ceiling;
			// UZDoom (HWWall::SkyTop): above a sky ceiling the line is covered by the sky wall when the other ceiling is not sky.
			const bool clipToSkyCeiling = PartB_IsSky(facingSector.Ceiling.TextureHandle) && !PartB_IsSky(otherSector.Ceiling.TextureHandle) &&
				!SkyWallsDisabled(facingSide, otherSector);

			WorldTriangulator::HandleTwoSidedLower(facingSide, top, bottom, texture->UVInverse, isFrontSide, wall);
			if (data == nullptr)
			{
				std::unique_ptr<DynamicVertexArray> created = GetWallVertices(wall, GetLightLevelAdd(facingSide), lightIndex, overrideLightIndex,
					GetWallLightLevel(facingSide, WallLocation::Lower), facingSide->linedef->Index(), WallLocation::Lower, texture->ArrayIndex);
				data = created.get();
				// UZDoom: slopes. The wall is bounded by the other floor above and the facing floor below.
				ApplyWallSlopes(data->data(), (int)data->size(), &top, &bottom);
				if (clipToSkyCeiling)
					ClipWallTop(data->data(), (int)data->size(), facingSector.Ceiling);
				SetWallLightmap(data->data(), facingSide, WallLocation::Lower, true);
				SetCachedSide(m_vertexLowerLookup, facingSide, std::move(created));
			}
			else
			{
				SetWallVertices(data->data(), wall, GetLightLevelAdd(facingSide), lightIndex, overrideLightIndex,
					GetWallLightLevel(facingSide, WallLocation::Lower), facingSide->linedef->Index(), WallLocation::Lower, texture->ArrayIndex);
				ApplyWallSlopes(data->data(), (int)data->size(), &top, &bottom);
				if (clipToSkyCeiling)
					ClipWallTop(data->data(), (int)data->size(), facingSector.Ceiling);
				SetWallLightmap(data->data(), facingSide, WallLocation::Lower, true);
			}
		}

		if (m_buffer)
		{
			GLLegacyTexture* arrayTexture = m_glTextureManager->GetParentArrayTexture(lowerHandle);
			Data::RenderWorldData& renderData = m_worldDataManager->GetRenderData(arrayTexture, GeometryType::Wall, brightmapTexture);
			CopyToRenderData(renderData, std::span<const DynamicVertex>(*data), m_dither && DitherTransparency::SideDithered(facingSide, side_t::bottom));
		}
		vertices = data;
		skyVertices = nullptr;
	}
}

bool GeometryRenderer::TwoSidedLowerFlood(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide)
{
	Portals::FloodSet result = Portals::FloodSet::None;
	const auto& floodKeys = RenderStaticState::Side(facingSide).LowerFloodKeys;
	if ((m_renderMode == GeometryRenderMode::Dynamic && (floodKeys.Key1 > 0 || floodKeys.Key2 > 0)) ||
		(m_renderMode == GeometryRenderMode::All && PartB_ShouldFloodLower(facingSide, facingSector, otherSector)))
	{
		result = Portals().UpdateStaticFloodFillSide(facingSide, otherSide, otherSector, SideTexture::Lower, isFrontSide);
	}

	// If partner side flood is set then return false to stop.
	if (((int)result & (int)Portals::FloodSet::Normal) != 0)
		return false;

	return true;
}

void GeometryRenderer::RenderTwoSidedUpper(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector,
	bool isFrontSide, DynamicVertexArray*& vertices, SkyVertexArray*& skyVertices, SkyVertexArray*& skyVertices2, const RenderSector* lightLevelSector,
	bool renderSkySide)
{
	vertices = nullptr;
	skyVertices = nullptr;
	skyVertices2 = nullptr;

	const RenderPlane& plane = otherSector.Ceiling;
	bool isSky = PartB_IsSky(plane.TextureHandle) && PartB_IsSky(facingSector.Ceiling.TextureHandle);

	if (m_vanillaRender && ((RenderStaticState::Side(facingSide).FloodTextures & SideTexture::Upper) == SideTexture::None || isSky))
	{
		if (!isSky || (isSky && PartB_IsBlocked(facingSide, facingSector, otherSector)))
			RenderCoverWall(WallLocation::Upper, facingSide, facingSector, otherSector, isFrontSide);
	}

	if (m_renderCoverOnly)
		return;

	const FTextureID upperTexture = facingSide->GetTexture(side_t::top);
	const bool noUpperTexture = PartB_NoTexture(upperTexture);
	bool renderSkySideOnly = TwoSidedUpperFloodRenderSkySide(facingSide, otherSide, facingSector, otherSector, isFrontSide);

	if (!PartB_IsSky(facingSector.Ceiling.TextureHandle) && noUpperTexture)
		return;

	CheckInvalidatedSideCounter(SideInvalidatedCount(facingSide, WallLocation::Upper), m_invalidatedCounter);

	WallVertices wall;
	const int upperHandle = PartB_Handle(upperTexture);
	GLLegacyTexture* texture = m_glTextureManager->GetTexture(upperHandle);
	GLLegacyTexture* brightmapTexture = m_glTextureManager->GetBrightmapTexture(upperHandle);

	RenderPlane top = facingSector.Ceiling;
	RenderPlane bottom = otherSector.Ceiling;

	// UZDoom (hw_walls.cpp HWWall::Process, upper texture): the facing sector's own floor obstructs the part of the wall below it,
	// unless both floors are sky or the line draws full height. A closed sector next to a lower ceiling (a pillar reaching into
	// a sky) therefore shows no upper wall from inside. Not for a 3D floor slice (the fake side of RenderWallSlices3D): its fake
	// facing sector is a zero height plane at the slice top, not the real floor.
	bool obstructedByFloor = false;
	if (!isSky && facingSide != m_fakeSide.get() && !(PartB_IsSky(facingSector.Floor.TextureHandle) && PartB_IsSky(otherSector.Floor.TextureHandle)) &&
		(facingSide->linedef->flags & ML_DRAWFULLHEIGHT) == 0 &&
		facingSector.Floor.Z > otherSector.Ceiling.Z && facingSector.Floor.PrevZ > otherSector.Ceiling.PrevZ)
	{
		obstructedByFloor = facingSector.Floor.Z >= facingSector.Ceiling.Z && facingSector.Floor.PrevZ >= facingSector.Ceiling.PrevZ;
		bottom = facingSector.Floor;
	}

	if (renderSkySide && (!isSky || renderSkySideOnly))
		RenderSkySide(facingSide, facingSector, &otherSector, texture, isFrontSide, skyVertices2);
	if (renderSkySideOnly)
		return;

	if (lightLevelSector == nullptr)
		lightLevelSector = &facingSector;

	const bool noCache = IsFogSide(facingSide) || facingSide == m_fakeSide.get();

	if (isSky)
	{
		if (!renderSkySide || skyVertices2 != nullptr ||
			(PartB_IsSky(otherSide->sector->GetTexture(sector_t::ceiling).GetIndex()) && !PartB_IsBlocked(facingSide, facingSector, otherSector)))
		{
			vertices = nullptr;
			skyVertices = nullptr;
			return;
		}

		SkyVertexArray* data = noCache ? nullptr : m_skyWallVertexUpperLookup[facingSide->Index()].get();
		if (SideOffsetChanged(facingSide) || m_sectorChangedLine || data == nullptr)
		{
			WorldTriangulator::HandleTwoSidedUpper(facingSide, top, bottom, texture->UVInverse, isFrontSide, wall, MaxSky);
			if (data == nullptr)
			{
				std::unique_ptr<SkyVertexArray> created = CreateSkyWallVertices(wall);
				data = created.get();
				if (noCache)
					m_noCacheSkyUpper = std::move(created);
				else
					m_skyWallVertexUpperLookup[facingSide->Index()] = std::move(created);
			}
			else
			{
				SetSkyWallVertices(*data, wall);
			}
		}

		if (m_buffer)
			m_skyRenderer->AddSectorSky(facingSide->sector, sector_t::ceiling, data->data(), (int)data->size());
		vertices = nullptr;
		skyVertices = data;
	}
	else
	{
		if ((noUpperTexture && skyVertices2 != nullptr) || obstructedByFloor || !UpperIsVisible(facingSide, facingSector, otherSector))
		{
			// This isn't the best spot for this but separating this logic would be difficult. (Sector 72 in skyshowcase.wad)
			vertices = nullptr;
			skyVertices = nullptr;
			return;
		}

		DynamicVertexArray* data = GetCachedSide(m_vertexUpperLookup, facingSide);

		if (SideOffsetChanged(facingSide) || m_sectorChangedLine || data == nullptr)
		{
			int overrideLightIndex = 0;
			int lightIndex = ::Helion::Render::Renderer::GetLightBufferIndex(facingSide, side_t::top, lightLevelSector->Sector, overrideLightIndex);
			WorldTriangulator::HandleTwoSidedUpper(facingSide, top, bottom, texture->UVInverse, isFrontSide, wall);
			if (data == nullptr)
			{
				std::unique_ptr<DynamicVertexArray> created = GetWallVertices(wall, GetLightLevelAdd(facingSide), lightIndex, overrideLightIndex,
					GetWallLightLevel(facingSide, WallLocation::Upper), facingSide->linedef->Index(), WallLocation::Upper, texture->ArrayIndex);
				data = created.get();
				// UZDoom: slopes. The wall is bounded by the facing ceiling above and the other ceiling below.
				ApplyWallSlopes(data->data(), (int)data->size(), &top, &bottom);
				SetWallLightmap(data->data(), facingSide, WallLocation::Upper, true);
				SetCachedSide(m_vertexUpperLookup, facingSide, std::move(created));
			}
			else
			{
				SetWallVertices(data->data(), wall, GetLightLevelAdd(facingSide), lightIndex, overrideLightIndex,
					GetWallLightLevel(facingSide, WallLocation::Upper), facingSide->linedef->Index(), WallLocation::Upper, texture->ArrayIndex);
				ApplyWallSlopes(data->data(), (int)data->size(), &top, &bottom);
				SetWallLightmap(data->data(), facingSide, WallLocation::Upper, true);
			}
		}

		if (m_buffer)
		{
			GLLegacyTexture* arrayTexture = m_glTextureManager->GetParentArrayTexture(upperHandle);
			Data::RenderWorldData& renderData = m_worldDataManager->GetRenderData(arrayTexture, GeometryType::Wall, brightmapTexture);
			CopyToRenderData(renderData, std::span<const DynamicVertex>(*data), m_dither && DitherTransparency::SideDithered(facingSide, side_t::top));
		}
		vertices = data;
		skyVertices = nullptr;
	}
}

bool GeometryRenderer::TwoSidedUpperFloodRenderSkySide(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide)
{
	Portals::FloodSet result = Portals::FloodSet::None;
	bool unusedSkyHack = false;
	const auto& floodKeys = RenderStaticState::Side(facingSide).UpperFloodKeys;
	// Helion StaticDataApplier.ShouldFloodUpper
	if ((m_renderMode == GeometryRenderMode::Dynamic && (floodKeys.Key1 > 0 || floodKeys.Key2 > 0)) ||
		(m_renderMode == GeometryRenderMode::All && PartB_NoTexture(facingSide->GetTexture(side_t::top)) &&
			(facingSector.Ceiling.Z > otherSector.Ceiling.Z || facingSector.Ceiling.PrevZ > otherSector.Ceiling.PrevZ) &&
			UpperIsVisibleOrFlood(facingSide, otherSide, facingSector, otherSector, unusedSkyHack)))
	{
		result = Portals().UpdateStaticFloodFillSide(facingSide, otherSide, otherSector, SideTexture::Upper, isFrontSide);
	}

	// Key2 is used for partner side flood. Still may need to draw the upper.
	// Flood only floods the upper texture portion. If the ceiling is a sky texture then the fake sky side needs to be rendered with RenderSkySide.
	if (((int)result & (int)Portals::FloodSet::Normal) != 0)
		return true;

	return false;
}

void GeometryRenderer::RenderCoverWall(WallLocation location, side_t* facingSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide)
{
	if (!m_vanillaRender || !m_buffer)
		return;

	DynamicVertexArray* vertices = RenderTwoSidedUpperOrLowerRaw(location, facingSide, facingSector, otherSector, isFrontSide);
	m_worldDataManager->AddCoverWallVertices(facingSide, std::span<DynamicVertex>(*vertices), location, location == WallLocation::Middle);
}

// Renders vertices for upper/lower. No checking for skies, flood fill etc.
DynamicVertexArray* GeometryRenderer::RenderTwoSidedUpperOrLowerRaw(WallLocation location, side_t* facingSide, const RenderSector& facingSector,
	const RenderSector& otherSector, bool isFrontSide)
{
	WallVertices wall;

	GLLegacyTexture* texture = m_glTextureManager->GetTexture(PartB_Handle(facingSide->GetTexture(side_t::bottom)));
	int lightIndex = ::Helion::Render::Renderer::GetLightBufferIndex(facingSector.Sector, LightBufferType::Wall);
	if (location == WallLocation::Upper)
	{
		WorldTriangulator::HandleTwoSidedUpper(facingSide, facingSector.Ceiling, otherSector.Ceiling, texture->UVInverse, isFrontSide, wall);
		SetWallVertices(m_wallVertices.data(), wall, GetLightLevelAdd(facingSide), lightIndex, lightIndex, 0, facingSide->linedef->Index(), location, 0);
		ApplyWallSlopes(m_wallVertices.data(), (int)m_wallVertices.size(), &facingSector.Ceiling, &otherSector.Ceiling);
	}
	else
	{
		WorldTriangulator::HandleTwoSidedLower(facingSide, otherSector.Floor, facingSector.Floor, texture->UVInverse, isFrontSide, wall);
		SetWallVertices(m_wallVertices.data(), wall, GetLightLevelAdd(facingSide), lightIndex, lightIndex, 0, facingSide->linedef->Index(), location, 0);
		ApplyWallSlopes(m_wallVertices.data(), (int)m_wallVertices.size(), &otherSector.Floor, &facingSector.Floor);
	}
	return &m_wallVertices;
}

void GeometryRenderer::RenderSkySide(side_t* facingSide, const RenderSector& facingSector, const RenderSector* otherSector, GLLegacyTexture* texture, bool isFront,
	SkyVertexArray*& skyVertices, bool fromFloor)
{
	skyVertices = nullptr;
	if (otherSector == nullptr)
	{
		if (!PartB_IsSky(facingSector.Ceiling.TextureHandle))
			return;
	}
	else
	{
		// UZDoom (hw_sky.cpp HWWall::SkyTop): a two-sided line only gets a sky wall when the facing ceiling is sky and sky walls
		// are not suppressed. Helion also draws one when only the other ceiling is sky.
		if (!PartB_IsSky(facingSector.Ceiling.TextureHandle) || SkyWallsDisabled(facingSide, *otherSector))
			return;
	}

	const RenderPlane& floor = facingSector.Floor;
	const RenderPlane& ceiling = facingSector.Ceiling;

	WallVertices wall;
	double bottomZ = facingSector.Ceiling.Z;
	// UZDoom (HWWall::SkyTop): when both ceilings are sky and the other sector is closed the sky starts at the other sector's ceiling.
	// Otherwise everything on this line above the facing sky ceiling is sky, the other sector's walls included (lower walls are
	// clipped to the facing ceiling, see ClipWallTop). Helion starts the sky at the other floor for sky blocked lines without an
	// upper texture, which shows the walls of a closed sector above the sky ceiling.
	if (otherSector != nullptr && PartB_IsSky(otherSector->Ceiling.TextureHandle) && PartB_IsBlocked(facingSide, facingSector, *otherSector))
		bottomZ = otherSector->Ceiling.Z;
	if (fromFloor)
		bottomZ = facingSector.Floor.Z;

	WorldTriangulator::HandleOneSided(facingSide, facingSide, floor, ceiling, texture->UVInverse, wall, bottomZ, MaxSky, isFront);

	SetSkyWallVertices(m_skyWallVertices, wall);
	skyVertices = &m_skyWallVertices;

	if (m_buffer)
		m_skyRenderer->AddSectorSky(facingSide->sector, sector_t::ceiling, m_skyWallVertices.data(), (int)m_skyWallVertices.size());
}

void GeometryRenderer::RenderSkySide(side_t* facingSide, const RenderSector& facingSector, SectorPlaneFace face, bool isFront, SkyVertexArray*& skyVertices)
{
	WallVertices wall;
	if (face == SectorPlaneFace::Floor)
	{
		WorldTriangulator::HandleOneSided(facingSide, facingSide, facingSector.Floor, facingSector.Ceiling, Helion::Geometry::Vec2F(0, 0), wall,
			facingSector.Floor.Z - MaxSky, facingSector.Floor.Z, isFront);
	}
	else
	{
		WorldTriangulator::HandleOneSided(facingSide, facingSide, facingSector.Floor, facingSector.Ceiling, Helion::Geometry::Vec2F(0, 0), wall,
			facingSector.Ceiling.Z, facingSector.Ceiling.Z + MaxSky, isFront);
	}

	SetSkyWallVertices(m_skyWallVertices, wall);
	skyVertices = &m_skyWallVertices;
}

void GeometryRenderer::RenderFogBarrier(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide,
	DynamicVertexArray*& vertices, const RenderSector* lightLevelSector, const MidTexSpan* restrictSpan)
{
	double save = WorldStatic::LineVertexGap;
	WorldStatic::LineVertexGap = 0;
	// UZDoom: the fog side's middle texture is the black texture, see RenderTwoSidedMiddle (IsFogSide).
	m_fogSide->sector = facingSide->sector;
	m_fogSide->linedef = facingSide->linedef;
	RenderTwoSidedMiddle(m_fogSide.get(), otherSide, facingSector, otherSector, isFrontSide, vertices, lightLevelSector, restrictSpan);
	WorldStatic::LineVertexGap = save;
}

void GeometryRenderer::RenderTwoSidedMiddle(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector,
	bool isFrontSide, DynamicVertexArray*& vertices, const RenderSector* lightLevelSector, const MidTexSpan* restrictSpan)
{
	const bool isFog = IsFogSide(facingSide);
	line_t* line = facingSide->linedef;
	const bool wrapMidTex = isFog || (facingSide->Flags & WALLF_WRAP_MIDTEX) != 0 || (line->flags & ML_WRAP_MIDTEX) != 0;

	int middleHandle = 0;
	GLLegacyTexture* texture;
	GLLegacyTexture* brightmapTexture = nullptr;
	if (isFog)
	{
		// Helion m_fogWall.TextureHandle = BlackTextureIndex
		texture = m_glTextureManager->BlackTexture;
	}
	else
	{
		middleHandle = PartB_Handle(facingSide->GetTexture(side_t::mid));
		texture = m_glTextureManager->GetTexture(middleHandle, wrapMidTex);
		brightmapTexture = m_glTextureManager->GetBrightmapTexture(middleHandle, wrapMidTex);
	}

	// UZDoom: Helion's TextureTransparency config is always on.
	float alpha = (float)std::clamp(line->alpha, 0.0, 1.0);
	DynamicVertexArray* data = GetCachedSide(m_vertexLookup, facingSide);

	CheckInvalidatedSideCounter(SideInvalidatedCount(facingSide, WallLocation::Middle), m_invalidatedCounter);

	if (SideOffsetChanged(facingSide) || m_sectorChangedLine || data == nullptr)
	{
		if (lightLevelSector == nullptr)
			lightLevelSector = &facingSector;

		// UZDoom: Helion temporarily restores line.RenderSegStart/End to the segment here. The UZDoom triangulator always
		// uses the line's v1/v2 (cover wall extension is not applied to the line), so there is nothing to save.

		MidTexOpening opening = GetMidTexOpening(facingSide, facingSector, otherSector, false);
		MidTexOpening prevOpening = GetMidTexOpening(facingSide, facingSector, otherSector, true);
		double offset = GetTransferHeightHackOffset(facingSide, otherSide, opening.BottomZ, opening.TopZ, false);
		double prevOffset = offset;

		if (offset != 0)
		{
			bool check = (line->flags & ML_DONTPEGBOTTOM) != 0 ? opening.BottomZ == prevOpening.BottomZ : opening.TopZ == prevOpening.TopZ;
			if (check)
				prevOffset = GetTransferHeightHackOffset(facingSide, otherSide, opening.BottomZ, opening.TopZ, true);
		}

		int overrideLightIndex = 0;
		int lightIndex = ::Helion::Render::Renderer::GetLightBufferIndex(facingSide, side_t::mid, lightLevelSector->Sector, overrideLightIndex);

		WallVertices wall;
		bool nothingVisible = false;
		WorldTriangulator::HandleTwoSidedMiddle(facingSide, texture->Dimension(), texture->UVInverse, opening, prevOpening, isFrontSide, wall,
			nothingVisible, offset, prevOffset, GetTwoSidedMiddleClipPlanes(facingSide, otherSide, facingSector, otherSector), true, restrictSpan);

		if (data == nullptr)
		{
			std::unique_ptr<DynamicVertexArray> created = GetWallVertices(wall, GetLightLevelAdd(facingSide), lightIndex, overrideLightIndex,
				GetWallLightLevel(facingSide, WallLocation::Middle), line->Index(), WallLocation::None, texture->ArrayIndex, alpha, 0);
			data = created.get();
			SetCachedSide(m_vertexLookup, facingSide, std::move(created));
		}
		else
		{
			SetWallVertices(data->data(), wall, GetLightLevelAdd(facingSide), lightIndex, overrideLightIndex,
				GetWallLightLevel(facingSide, WallLocation::Middle), line->Index(), WallLocation::None, texture->ArrayIndex, alpha, 0);
		}

		SetWallLightmap(data->data(), facingSide, WallLocation::Middle, true);
	}

	if (m_buffer)
	{
		GeometryType geometryType = isFog ? GeometryType::FogBarrier : GetWallType(facingSide, WallLocation::Middle, nullptr);
		GLLegacyTexture* arrayTexture = isFog ? (texture->ParentArrayTexture != nullptr ? texture->ParentArrayTexture : texture) :
			m_glTextureManager->GetParentArrayTexture(middleHandle, wrapMidTex);
		Data::RenderWorldData& renderData = m_worldDataManager->GetRenderData(arrayTexture, geometryType, brightmapTexture);
		CopyToRenderData(renderData, std::span<const DynamicVertex>(*data), m_dither && DitherTransparency::SideDithered(facingSide, side_t::mid));
	}
	vertices = data;
}

GeometryRenderer::SectorPlanes GeometryRenderer::GetTwoSidedMiddleClipPlanes(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector,
	const RenderSector& otherSector)
{
	const RenderPlane& floor = facingSector.Floor;
	const RenderPlane& ceiling = facingSector.Ceiling;
	const RenderPlane& otherCeil = otherSector.Ceiling;

	bool midTextureHack = floor.MidTextureHack || ceiling.MidTextureHack;
	bool isCeilingSky = PartB_IsSky(otherCeil.TextureHandle) && PartB_IsSky(ceiling.TextureHandle);
	SectorPlanes clipPlanes = midTextureHack ? SectorPlanes::None : (SectorPlanes::Floor | SectorPlanes::Ceiling);
	if (isCeilingSky)
		clipPlanes = clipPlanes & SectorPlanes::Floor;

	if (!LowerIsVisible(facingSide, facingSector, otherSector))
		clipPlanes = clipPlanes & SectorPlanes::Ceiling;

	bool skyHack;
	if (!UpperOrSkySideIsVisible(facingSide, facingSector, otherSector, skyHack))
		clipPlanes = clipPlanes & SectorPlanes::Floor;

	return clipPlanes;
}

GeometryRenderer::SectorPlanes GeometryRenderer::GetTwoSidedMiddleClipPlanesVanilla(const RenderSector& facingSector, const RenderSector& otherSector)
{
	const RenderPlane& floor = facingSector.Floor;
	const RenderPlane& otherFloor = otherSector.Floor;
	const RenderPlane& ceiling = facingSector.Ceiling;
	const RenderPlane& otherCeil = otherSector.Ceiling;
	SectorPlanes clipPlanes = SectorPlanes::None;
	bool isCeilingSky = PartB_IsSky(otherCeil.TextureHandle) && PartB_IsSky(ceiling.TextureHandle);

	if (floor.LightLevelAdd != otherFloor.LightLevelAdd || floor.Z != otherSector.Floor.Z || floor.TextureHandle != otherFloor.TextureHandle)
		clipPlanes = clipPlanes | SectorPlanes::Floor;

	if (!isCeilingSky && (ceiling.LightLevelAdd != otherCeil.LightLevelAdd || ceiling.Z != otherCeil.Z || ceiling.TextureHandle != otherCeil.TextureHandle))
		clipPlanes = clipPlanes | SectorPlanes::Ceiling;

	return clipPlanes;
}

// There is some issue with how the original code renders middle textures with transfer heights.
// It appears to incorrectly draw from the floor of the original sector instead of the transfer heights sector.
// Alternatively, I could be dumb and this is dumb but it appears to work.
double GeometryRenderer::GetTransferHeightHackOffset(side_t* facingSide, side_t* otherSide, double bottomZ, double topZ, bool previous)
{
	if (otherSide->sector->GetHeightSec() == nullptr && facingSide->sector->GetHeightSec() == nullptr)
		return 0;

	MidTexOpening openingFlats = GetMidTexOpening(facingSide, RenderSector(facingSide->sector), RenderSector(otherSide->sector), previous);
	if ((facingSide->linedef->flags & ML_DONTPEGBOTTOM) != 0)
		return openingFlats.BottomZ - bottomZ;

	return openingFlats.TopZ - topZ;
}

GeometryRenderer::MidTexOpening GeometryRenderer::GetMidTexOpening(side_t* facingSide, const RenderSector& facingSector, const RenderSector& otherSector, bool previous)
{
	const RenderPlane& facingFloor = facingSector.Floor;
	const RenderPlane& facingCeiling = facingSector.Ceiling;
	const RenderPlane& otherFloor = otherSector.Floor;
	const RenderPlane& otherCeiling = otherSector.Ceiling;

	double facingFloorZ, facingCeilingZ, otherFloorZ, otherCeilingZ;
	if (previous)
	{
		facingFloorZ = facingFloor.PrevZ;
		facingCeilingZ = facingCeiling.PrevZ;
		otherFloorZ = otherFloor.PrevZ;
		otherCeilingZ = otherCeiling.PrevZ;
	}
	else
	{
		facingFloorZ = facingFloor.Z;
		facingCeilingZ = facingCeiling.Z;
		otherFloorZ = otherFloor.Z;
		otherCeilingZ = otherCeiling.Z;
	}

	double bottomZ = std::max(facingFloorZ, otherFloorZ);
	double topZ = std::min(facingCeilingZ, otherCeilingZ);
	double minBottomZ = bottomZ;
	double maxTopZ = topZ;

	bool skyHack;
	if (LowerIsVisible(facingSide, facingSector, otherSector) && PartB_NoTexture(facingSide->GetTexture(side_t::bottom)))
		minBottomZ = std::min(facingFloorZ, otherFloorZ);
	if (UpperOrSkySideIsVisible(facingSide, facingSector, otherSector, skyHack) && PartB_NoTexture(facingSide->GetTexture(side_t::top)))
		maxTopZ = std::max(facingCeilingZ, otherCeilingZ);

	MidTexOpening opening;
	opening.BottomZ = bottomZ;
	opening.TopZ = topZ;
	opening.MinBottomZ = minBottomZ;
	opening.MaxTopZ = maxTopZ;
	return opening;
}

GeometryRenderer::MidTexSpan GeometryRenderer::GetMidTexSpan(const Helion::Geometry::Dimension& dimension, side_t* front, side_t* back, const RenderSector& frontSector,
	const RenderSector& backSector, double offset, double prevOffset)
{
	WallVertices wall;
	MidTexOpening opening = GetMidTexOpening(front, RenderSector(front->sector), backSector, false);
	MidTexOpening prevOpening = GetMidTexOpening(front, RenderSector(front->sector), backSector, true);
	offset += GetTransferHeightHackOffset(front, back, opening.BottomZ, opening.TopZ, false);
	prevOffset += GetTransferHeightHackOffset(front, back, prevOpening.BottomZ, prevOpening.TopZ, true);
	bool nothingVisible = false;
	WorldTriangulator::HandleTwoSidedMiddle(front, dimension, Helion::Geometry::Vec2F(0, 0), opening, prevOpening, true, wall, nothingVisible,
		offset, prevOffset, SectorPlanes::Floor | SectorPlanes::Ceiling, false);
	return MidTexSpan(wall.BottomRight.Z, wall.TopLeft.Z, wall.PrevBottomZ, wall.PrevTopZ);
}

GeometryType GeometryRenderer::GetWallType(side_t* side, WallLocation wall, Sector3D* sector3D)
{
	if (sector3D != nullptr && (wall == WallLocation::Middle3D || wall == WallLocation::Middle))
	{
		if (sector3D->RenderDataStyle != Data::RenderDataStyle::Normal)
			return ToGeometryType(sector3D->RenderDataStyle);
		if (sector3D->Alpha < 1)
			return GeometryType::Translucent;
	}

	// UZDoom: Helion Side.RenderDataStyle of a level side is derived from the line: additive lines (ML_ADDTRANS) are Add.
	Data::RenderDataStyle sideStyle = Data::RenderDataStyle::Normal;
	if (IsFogSide(side))
		sideStyle = Data::RenderDataStyle::FogBarrier;
	else if ((side->linedef->flags & ML_ADDTRANS) != 0)
		sideStyle = Data::RenderDataStyle::Add;

	if (wall == WallLocation::Middle && (side->linedef->alpha < 1 || sideStyle != Data::RenderDataStyle::Normal))
	{
		if (sideStyle != Data::RenderDataStyle::Normal)
			return ToGeometryType(sideStyle);
		return GeometryType::Translucent;
	}

	if (wall == WallLocation::Middle3D)
	{
		// UZDoom: the 3D floor wall texture is the middle texture of the control side.
		side_t* textureSide = sector3D != nullptr && sector3D->ControlSide != nullptr ? sector3D->ControlSide : side;
		if (m_glTextureManager->GetTexture(PartB_Handle(textureSide->GetTexture(side_t::mid)))->TransparentPixelCount > 0)
			return GeometryType::TwoSidedMiddleWall;

		return GeometryType::Middle3D;
	}

	return wall == WallLocation::Middle && side->linedef->backsector != nullptr ? GeometryType::TwoSidedMiddleWall : GeometryType::Wall;
}

void GeometryRenderer::SetRenderMode(GeometryRenderMode renderMode, TransferHeightView view, bool newTick)
{
	m_renderMode = renderMode;
	m_prevTransferHeightsView = m_transferHeightsView;
	m_transferHeightsView = view;

	if (m_prevTransferHeightsView != m_transferHeightsView)
		m_invalidatedCounter++;

	// UZDoom: Helion's Developer.Render.Lock config does not exist (always off).
	bool clearFloodVertices = true;
	if (clearFloodVertices && !newTick)
		clearFloodVertices = false;

	Portals().SetRenderMode(renderMode, view, clearFloodVertices);
	SetBufferCoverWall(true);
}

void GeometryRenderer::SetViewPosition(const Helion::Geometry::Vec3D& viewPosition, const Helion::Geometry::Vec3D& prevViewPosition)
{
	m_viewPosition = viewPosition;
	m_prevViewPosition = prevViewPosition;
}

void GeometryRenderer::SetBufferCoverWall(bool set)
{
	if (!m_vanillaRender)
		return;
	m_worldDataManager->BufferCoverWalls = set;
}



// ---- Helion GeometryRenderer.cs lines 1780-2409
// NEEDS (includes at the top of GeometryRenderer.cpp):
//   #include <algorithm>
//   #include "g_levellocals.h"
//   #include "r_defs.h"
//   #include "r_data/r_sections.h"
//   #include "r_sky.h"
//   #include "OpenGL/Renderers/Legacy/World/Geometry/Static/StaticCacheGeometryRenderer.h"
//   #include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalRenderer.h"
//   #include "Renderer.h"
//   #include "Common/Shared/World/WorldTriangulator.h"
//   #include "OpenGL/Renderers/Legacy/World/VertexOptions.h"
//   #include "OpenGL/Renderers/Legacy/World/UvFlags.h"
//   #include "OpenGL/Renderers/Legacy/World/Geometry/Static/LightBufferType.h"
// NEEDS (using declarations): none (everything is qualified or an alias of the class).
// Uses the class members m_sector3DTable, m_sectionIndices, m_subsectorVertices, m_flatInvalidatedCounts (new, declared in the header).
// Helion lines 1766-1779 (public RenderSectorFlats) through 2409.

namespace
{
	using WorldTriangulatorPartC = ::Helion::Render::OpenGL::Shared::World::WorldTriangulator;
	struct FlatLightmapVertex { float U = 0; float V = 0; float Index = -1; };

	// Helion computes (SegCount - 2) * 3 per subsector. UZDoom flats are built per FSection, which is the union of its subsectors; the polygon
	// of a subsector with numlines >= 3 produces (numlines - 2) triangles (WorldTriangulator::HandleSubsectorIndices).
	size_t SectionTriangleVertexCount(TArrayView<FSection*> sections)
	{
		size_t count = 0;
		for (unsigned i = 0; i < sections.Size(); i++)
		{
			for (subsector_t* subsector : sections[i]->subsectors)
			{
				if (subsector->numlines >= 3)
					count += (size_t)(subsector->numlines - 2) * 3;
			}
		}
		return count;
	}

	// UZDoom: Helion fake sectors carry Sector3D != null. The 3D planes (Sector3D FakeTop/FakeBottom(+Flipped)) have a RenderPlane::Id outside
	// of the real planes range (sector index * 2 + face, see RenderPlane::From).
	bool IsGeometryPlane3D(const FLevelLocals* world, int planeId)
	{
		return planeId >= (int)world->sectors.Size() * 2;
	}

	// Writes the 6 vertices (2 triangles) of a wall. Our triangles are added like:
	//    0--2
	//    | /  4
	//    |/  /|
	//    1  / |
	//      5--3
	// Per vertex: x corner (0 = TopLeft, 1 = BottomRight; X, Y, U, PrevU), vertical corner (0 = TopLeft, 1 = BottomRight; Z, V, PrevZ, PrevV),
	// and whether the vertex is flagged as top left in the surface options (Helion packs 1 for vertices 0-2).
	struct WallVertexLayout
	{
		int XCorner;
		int ZCorner;
		int TopLeft;
	};

	constexpr WallVertexLayout WallLayout[6] =
	{
		{ 0, 0, 1 },
		{ 0, 1, 1 },
		{ 1, 0, 1 },
		{ 1, 1, 0 },
		{ 1, 0, 0 },
		{ 0, 1, 0 },
	};
}

void GeometryRenderer::RenderSectorFlats(const RenderSector& renderSector, const RenderPlane& renderPlane, const RenderPlane& geometryPlane, bool floor, bool renderFlood,
	DynamicVertexArray*& vertices, SkyVertexArray*& skyVertices, const RenderSector* lightLevelSector, bool allowAlpha, float alpha, Data::RenderDataStyle style)
{
	vertices = nullptr;
	skyVertices = nullptr;

	// Helion: if (renderSector.Id >= m_subsectors.Length) return;
	if (m_world == nullptr || renderSector.Id < 0 || renderSector.Id >= (int)m_world->sectors.Size())
		return;

	TArrayView<FSection*> sections = SectionsOfSector(&m_world->sectors[renderSector.Id]);
	if (sections.Size() == 0)
		return;

	GLLegacyTexture* texture = nullptr;
	RenderFlat(sections, renderPlane, geometryPlane, floor, renderFlood, false, vertices, skyVertices, texture,
		lightLevelSector, allowAlpha, alpha, style);
}

// Doom would render flats with no texture ("-") as black. If the flat isn't flagged to allow alpha then the black texture must be used.
// UZDoom: TexMan has no black texture index, see BlackTextureHandle. The "-" flat is texture index 0 (invalid ids are negative).
int GeometryRenderer::GetFlatTextureHandle(int textureHandle, bool allowAlpha) const
{
	return !allowAlpha && textureHandle <= 0 ? BlackTextureHandle : textureHandle;
}

GeometryRenderer::GLLegacyTexture* GeometryRenderer::GetFlatGLTexture(int flatTextureHandle)
{
	if (flatTextureHandle == BlackTextureHandle)
		return m_glTextureManager->BlackTexture;
	return m_glTextureManager->GetTexture(flatTextureHandle);
}

bool GeometryRenderer::RenderFlat(TArrayView<FSection*> sections, const RenderPlane& renderPlane, const RenderPlane& geometryPlane, bool floor, bool renderFlood, bool checkViewPos,
	DynamicVertexArray*& vertices, SkyVertexArray*& skyVertices, GLLegacyTexture*& texture,
	const RenderSector* lightLevelRenderSector, bool allowAlpha, float alpha, Data::RenderDataStyle style)
{
	vertices = nullptr;
	skyVertices = nullptr;
	texture = nullptr;

	if (sections.Size() == 0)
		return false;

	int textureHandle = GetFlatTextureHandle(renderPlane.TextureHandle, allowAlpha);
	bool isSky = textureHandle == skyflatnum.GetIndex();

	bool generateSector3D = IsGeometryPlane3D(m_world, geometryPlane.Id);

	if (renderPlane.NoRender || (checkViewPos && !isSky && !generateSector3D && ViewPositionForFlatInvalid(renderPlane, floor)))
		return false;

	// UZDoom: the ceiling of a closed sector has no open space below it and is never visible in the classic renderer (the clipper treats
	// the sector as solid). In static mode it would show through the sky hack gap of its sides when it reaches above a neighbouring sky
	// ceiling (skytest MAP01 pillars), so it is not emitted.
	if (!floor && !isSky && !generateSector3D && sections[0]->sector->isClosed())
		return false;

	// UZDoom (plan M5.2, HWFlat::ProcessSector): a plane with a sector portal or a reflection is drawn by the portal driver. Its
	// flat is drawn over it with the alpha of the portal (1 - reflect for mirrors), translucent below 1, not at all at 0; a sky
	// textured portal plane is neither a flat nor level sky.
	if (!generateSector3D)
	{
		sector_t* planeSector = sections[0]->sector;
		int planeIndex = floor ? sector_t::floor : sector_t::ceiling;
		if (Portals::PortalDriver::PlaneIsPortal(planeSector, planeIndex))
		{
			if (isSky)
				return false;
			float portalAlpha = Portals::PortalDriver::PlaneAlpha(planeSector, planeIndex);
			if (portalAlpha <= 0)
				return false;
			if (portalAlpha < 1)
			{
				allowAlpha = true;
				alpha = portalAlpha;
				style = Data::RenderDataStyle::Translucent;
			}
		}
	}

	// UZDoom: the sky flat is never drawn with its own texture and the GL texture of the sky flat is not needed (the sky renderer owns the
	// sky textures), so it is not created. UVs of sky flats are not used.
	GLLegacyTexture* glTexture = isSky ? nullptr : GetFlatGLTexture(textureHandle);
	GLLegacyTexture* brightmapTexture = (isSky || textureHandle < 0) ? nullptr : m_glTextureManager->GetBrightmapTexture(textureHandle);
	texture = glTexture;

	GeometryType geometryType = GetGeometryType(style, GeometryType::Flat);
	bool flatChanged = FlatChanged(renderPlane);
	// Helion: CheckInvalidatedFlatCounter(ref geometryPlane.InvalidatedCount, ...), the counter is stored per plane id here.
	flatChanged |= CheckInvalidatedFlatCounter(m_flatInvalidatedCounts[geometryPlane.Id], m_invalidatedCounter);
	sector_t* sector = sections[0]->sector;
	int id = sector->Index();
	Helion::Geometry::Vec2F textureVector = glTexture != nullptr ?
		Helion::Geometry::Vec2F((float)glTexture->Dimension().Width, (float)glTexture->Dimension().Height) : Helion::Geometry::Vec2F(64, 64);

	// Helion: `subsector.Flood && !renderPlane.MidTextureHack` subsectors are left out (the flood fill of the sector covers them), sky
	// planes only for floors. UZDoom's flooded subsectors are the open sector render hacks (SetFloodSectors).
	bool skipFloodSky = floor && !renderPlane.MidTextureHack;
	bool skipFlood = !renderFlood && !renderPlane.MidTextureHack;

	size_t indexStart = 0;
	if (isSky)
	{
		bool generate;
		SkyVertexArray* lookupData = GetSkySectorVertices(sections, floor, id, generate);
		if (generate || flatChanged || generateSector3D)
		{
			for (unsigned j = 0; j < sections.Size(); j++)
			{
				WorldTriangulatorPartC::HandleSection(sections[j], renderPlane, floor, textureVector, m_subsectorVertices, m_sectionIndices,
					floor || generateSector3D ? renderPlane.Z : MaxSky, skipFloodSky);
				for (unsigned i = 0; i + 2 < m_sectionIndices.Size(); i += 3)
				{
					if (lookupData->size() < indexStart + 3)
						lookupData->resize(indexStart + 3);
					CreateSkyFlatVertices(*lookupData, (int)indexStart, m_subsectorVertices[m_sectionIndices[i]], m_subsectorVertices[m_sectionIndices[i + 1]],
						m_subsectorVertices[m_sectionIndices[i + 2]]);
					indexStart += 3;
				}
			}

			if (lookupData->size() != indexStart)
				lookupData->resize(indexStart);
		}

		vertices = nullptr;
		skyVertices = lookupData;

		if (m_buffer)
			m_skyRenderer->AddSectorSky(sector, floor ? sector_t::floor : sector_t::ceiling, lookupData->data(), (int)lookupData->size());
	}
	else
	{
		bool generate;
		DynamicVertexArray* lookupData = generateSector3D ? GetSectorVertices3D(sections, geometryPlane, generate) : GetSectorVertices(sections, floor, id, generate);
		if (generate || flatChanged)
		{
			sector_t* lightLevelSector = lightLevelRenderSector != nullptr ? lightLevelRenderSector->Sector :
				::Helion::World::Geometry::Sectors::GetRenderSector(sector, m_transferHeightsView).Sector;

			int lightIndex;
			int overrideLightIndex;
			// Helion: sector.TransferFloorLightSector.Floor / sector.TransferCeilingLightSector.Ceiling. Helion's RenderSectorFlats temporarily
			// sets the floor light sector of the parent sector to itself while it renders the flats of a 3D sector.
			sector_t* lightSector = sector;
			bool lightFrom3D = false;

			if (floor)
				lightIndex = ::Helion::Render::Renderer::GetLightBufferIndex(lightLevelSector, (int)SectorPlaneFace::Floor, Static::LightBufferType::Floor, overrideLightIndex);
			else
				lightIndex = ::Helion::Render::Renderer::GetLightBufferIndex(lightLevelSector, (int)SectorPlaneFace::Ceiling, Static::LightBufferType::Ceiling, overrideLightIndex);

			if (!generateSector3D && sector->e->XFloor.ffloors.Size() != 0)
			{
				// UZDoom: the real planes of a sector with 3D floors take the light of the 3D floor above them from the sector's light
				// list, exactly like HWFlat::ProcessSector (P_GetPlaneLight: shadows, FF_NOSHADE, FF_DOUBLESHADOW, FF_RESET and sloped
				// planes measured at the sector centre). The 3D floor light replaces the plane light level, the plane's relative light
				// is not added. Helion's TransferFloorLightSector (scalar plane heights) left the floor under a shadowing floor lit.
				const int planePos = floor ? sector_t::floor : sector_t::ceiling;
				lightlist_t* light = P_GetPlaneLight(sector, floor ? &sector->floorplane : &sector->ceilingplane, !floor);
				const bool absolute = (sector->GetFlags(planePos) & PLANEF_ABSLIGHTING) != 0;
				if ((!absolute || (floor && light->lightsource == nullptr)) && light->p_lightlevel != &sector->lightlevel && light->lightsource != nullptr)
				{
					lightSector = light->lightsource->model;
					lightFrom3D = true;
					overrideLightIndex = 0;
					lightIndex = ::Helion::Render::Renderer::GetLightBufferIndex(lightSector, floor ? Static::LightBufferType::Floor : Static::LightBufferType::Ceiling);
				}
			}
			else if (!floor && m_sector3DTable.HasSectors3D(sector))
			{
				if (sector_t* transfer = m_sector3DTable.TransferCeilingLightSector(sector))
					lightSector = transfer;
			}

			int lightPlaneLevel = lightFrom3D ? 0 : lightSector->GetPlaneLight(floor ? sector_t::floor : sector_t::ceiling);
			// UZDoom: signed, relative plane light can be negative (VertexOptions::PackRender).
			int flatLightLevel = std::clamp(lightPlaneLevel, -256, 255);
			int upper = floor ? 0 : 1;
			int lower = 1 - upper;
			int addAlpha = allowAlpha ? 0 : 1;

			for (unsigned j = 0; j < sections.Size(); j++)
			{
				WorldTriangulatorPartC::HandleSection(sections[j], renderPlane, floor, textureVector, m_subsectorVertices, m_sectionIndices, INT32_MAX, skipFlood);

				// UZDoom: lightmaps. Per polygon vertex lightmap UV (hw_vertexbuilder.cpp CreateIndexedSectorVerticesLM): the vertex order of
				// HandleSubsector is reversed for floors, subsectors with less than 3 lines are skipped like HandleSection does.
				std::vector<FlatLightmapVertex> lightmapVertices;
				if (!generateSector3D && sector->HasLightmaps)
				{
					lightmapVertices.resize(m_subsectorVertices.Size());
					unsigned pos = 0;
					for (subsector_t* sub : sections[j]->subsectors)
					{
						if (!WorldTriangulatorPartC::IncludeSubsector(sub, skipFlood))
							continue;
						const LightmapSurface* surface = sub->lightmap[floor ? 0 : 1] != nullptr ? &sub->lightmap[floor ? 0 : 1][0] : nullptr;
						if (surface != nullptr && surface->Type != ST_NULL && surface->TexCoords != nullptr)
						{
							for (unsigned k = 0; k < sub->numlines && pos + k < lightmapVertices.size(); k++)
							{
								unsigned edge = floor ? sub->numlines - 1 - k : k;
								lightmapVertices[pos + k] = { surface->TexCoords[edge * 2], surface->TexCoords[edge * 2 + 1], (float)surface->LightmapNum };
							}
						}
						pos += sub->numlines;
					}
				}
				for (unsigned i = 0; i + 2 < m_sectionIndices.Size(); i += 3)
				{
					if (lookupData->size() < indexStart + 3)
						lookupData->resize(indexStart + 3);
					GetFlatVertices(*lookupData, (int)indexStart, m_subsectorVertices[m_sectionIndices[i]], m_subsectorVertices[m_sectionIndices[i + 1]],
						m_subsectorVertices[m_sectionIndices[i + 2]], lightIndex, overrideLightIndex, flatLightLevel, upper, lower, addAlpha, alpha, glTexture->ArrayIndex);
					if (!lightmapVertices.empty())
					{
						for (int v = 0; v < 3; v++)
						{
							const FlatLightmapVertex& lm = lightmapVertices[m_sectionIndices[i + v]];
							DynamicVertex& vertex = (*lookupData)[indexStart + v];
							vertex.LightmapU = lm.U;
							vertex.LightmapV = lm.V;
							vertex.LightmapIndex = lm.Index;
						}
					}
					indexStart += 3;
				}
			}

			if (lookupData->size() != indexStart)
				lookupData->resize(indexStart);
		}

		skyVertices = nullptr;
		vertices = lookupData;
		if (m_buffer)
		{
			GLLegacyTexture* arrayTexture = glTexture->ParentArrayTexture != nullptr ? glTexture->ParentArrayTexture : glTexture;
			Data::RenderWorldData& renderData = m_worldDataManager->GetRenderData(arrayTexture, geometryType, brightmapTexture);
			CopyToRenderData(renderData, std::span<const DynamicVertex>(lookupData->data(), lookupData->size()), m_dither && geometryPlane.Plane().dithertransflag);
			// Don't need to clip floor on lower view and ceiling on upper view
			if (sector->GetHeightSec() != nullptr
				&& !(m_transferHeightsView == TransferHeightView::Bottom && floor)
				&& !(m_transferHeightsView == TransferHeightView::Top && !floor))
			{
				m_worldDataManager->AddCoverFlatVertices(std::span<const DynamicVertex>(lookupData->data(), lookupData->size()));
			}
		}
	}

	return true;
}

bool GeometryRenderer::ViewPositionForFlatInvalid(const RenderPlane& renderPlane, bool floor)
{
	if (floor)
		return m_viewPosition.Z < renderPlane.Z && m_prevViewPosition.Z < renderPlane.PrevZ;
	return m_viewPosition.Z > renderPlane.Z && m_prevViewPosition.Z > renderPlane.PrevZ;
}

DynamicVertexArray* GeometryRenderer::GetSectorVertices3D(TArrayView<FSection*> sections, const RenderPlane& geometryPlane, bool& generate)
{
	auto it = m_vertexPlaneLookup3D.find(geometryPlane.Id);
	if (it != m_vertexPlaneLookup3D.end())
	{
		generate = false;
		return it->second.get();
	}

	generate = true;
	// Helion: InitSectorVertices(subsectors, 0, EmptyLookup) with a throwaway lookup; the array is stored in the plane lookup.
	auto data = std::make_unique<DynamicVertexArray>(SectionTriangleVertexCount(sections));
	DynamicVertexArray* result = data.get();
	m_vertexPlaneLookup3D[geometryPlane.Id] = std::move(data);
	return result;
}

DynamicVertexArray* GeometryRenderer::GetSectorVertices(TArrayView<FSection*> sections, bool floor, int id, bool& generate)
{
	auto& lookup = floor ? m_vertexFloorLookup : m_vertexCeilingLookup;
	// UZDoom: the lookups are sized by UpdateFlatVertices() at level load; grow if a sector index is outside of them.
	if ((size_t)id >= lookup.size())
		lookup.resize((size_t)id + 1);

	DynamicVertexArray* data = lookup[id].get();
	generate = data == nullptr;
	if (data == nullptr)
		data = InitSectorVertices(sections, id, lookup);
	return data;
}

SkyVertexArray* GeometryRenderer::GetSkySectorVertices(TArrayView<FSection*> sections, bool floor, int id, bool& generate)
{
	auto& lookup = floor ? m_skyFloorVertexLookup : m_skyCeilingVertexLookup;
	if ((size_t)id >= lookup.size())
		lookup.resize((size_t)id + 1);

	SkyVertexArray* data = lookup[id].get();
	generate = data == nullptr;
	if (data == nullptr)
		data = InitSkyVertices(sections, id, lookup);
	return data;
}

DynamicVertexArray* GeometryRenderer::InitSectorVertices(TArrayView<FSection*> sections, int id, std::vector<std::unique_ptr<DynamicVertexArray>>& lookup)
{
	lookup[id] = std::make_unique<DynamicVertexArray>(SectionTriangleVertexCount(sections));
	return lookup[id].get();
}

SkyVertexArray* GeometryRenderer::InitSkyVertices(TArrayView<FSection*> sections, int id, std::vector<std::unique_ptr<SkyVertexArray>>& lookup)
{
	lookup[id] = std::make_unique<SkyVertexArray>(SectionTriangleVertexCount(sections));
	return lookup[id].get();
}

bool GeometryRenderer::FlatChanged(const RenderPlane& flat)
{
	if (flat.Facing == SectorPlaneFace::Floor)
		return m_floorChanged;
	else
		return m_ceilingChanged;
}

void GeometryRenderer::SetSkyWallVertices(SkyGeometryVertex* data, const WallVertices& wv)
{
	// Same corners as the dynamic wall: (x corner, vertical corner) per vertex, see WallLayout.
	const TriangulatedWorldVertex* xCorner[2] = { &wv.TopLeft, &wv.BottomRight };
	const float prevZ[2] = { wv.PrevTopZ, wv.PrevBottomZ };
	// The sky wall of Helion writes TopLeft.Z for the top-right vertex pair (vertices 2 and 3), which is kept as is.
	static constexpr int zCorner[6] = { 0, 1, 0, 0, 1, 1 };
	static constexpr int xIndex[6] = { 0, 0, 1, 1, 0, 1 };

	for (int i = 0; i < 6; i++)
	{
		SkyGeometryVertex& vertex = data[i];
		vertex.X = xCorner[xIndex[i]]->X;
		vertex.Y = xCorner[xIndex[i]]->Y;
		vertex.Z = xCorner[zCorner[i]]->Z;
		vertex.PrevZ = prevZ[zCorner[i]];
	}
}

void GeometryRenderer::SetSkyWallVertices(SkyVertexArray& data, const WallVertices& wv)
{
	SetSkyWallVertices(data.data(), wv);
}

std::unique_ptr<SkyVertexArray> GeometryRenderer::CreateSkyWallVertices(const WallVertices& wv)
{
	auto data = std::make_unique<SkyVertexArray>(6);
	SetSkyWallVertices(data->data(), wv);
	return data;
}

void GeometryRenderer::CreateSkyFlatVertices(SkyVertexArray& vertices, int startIndex, const TriangulatedWorldVertex& root, const TriangulatedWorldVertex& second,
	const TriangulatedWorldVertex& third)
{
	SkyGeometryVertex* vertex = &vertices[startIndex];
	vertex->X = root.X;
	vertex->Y = root.Y;
	vertex->Z = root.Z;
	vertex->PrevZ = root.PrevZ;

	vertex++;
	vertex->X = second.X;
	vertex->Y = second.Y;
	vertex->Z = second.Z;
	vertex->PrevZ = second.PrevZ;

	vertex++;
	vertex->X = third.X;
	vertex->Y = third.Y;
	vertex->Z = third.Z;
	vertex->PrevZ = third.PrevZ;
}

void GeometryRenderer::SetWallVertices(DynamicVertex* data, const WallVertices& wv, int lightLevelAdd, int lightIndex, int overrideLightIndex, int wallLightLevel,
	int mapId, WallLocation location, int textureIndex, float alpha, int addAlpha)
{
	// UvFlags is not a flags enum in C++: Normal = 0, MirrorX = 1, MirrorY = 2.
	int uvFlags = (int)UvFlags::Normal;
	if (wv.TopLeft.U > wv.BottomRight.U)
		uvFlags |= (int)UvFlags::MirrorX;
	if (wv.TopLeft.V > wv.BottomRight.V)
		uvFlags |= (int)UvFlags::MirrorY;
	float renderOptions = VertexOptions::PackRender(lightIndex, wallLightLevel, (UvFlags)uvFlags);
	float lightLevelAddAndMapId = VertexOptions::LightLevelAdd(mapId, lightLevelAdd);
	int lower = location == WallLocation::Lower ? 1 : 0;
	int upper = location == WallLocation::Upper ? 1 : 0;
	float surfaceTopLeft = VertexOptions::PackSurface(1, alpha, addAlpha, upper, lower, overrideLightIndex, textureIndex);
	float surfaceOther = VertexOptions::PackSurface(0, alpha, addAlpha, upper, lower, overrideLightIndex, textureIndex);

	const TriangulatedWorldVertex* corner[2] = { &wv.TopLeft, &wv.BottomRight };
	const float prevZ[2] = { wv.PrevTopZ, wv.PrevBottomZ };

	for (int i = 0; i < 6; i++)
	{
		const TriangulatedWorldVertex& x = *corner[WallLayout[i].XCorner];
		const TriangulatedWorldVertex& z = *corner[WallLayout[i].ZCorner];
		DynamicVertex& vertex = data[i];
		vertex.X = x.X;
		vertex.Y = x.Y;
		vertex.Z = z.Z;
		vertex.PrevX = x.X;
		vertex.PrevY = x.Y;
		vertex.PrevZ = prevZ[WallLayout[i].ZCorner];
		vertex.U = x.U;
		vertex.V = z.V;
		vertex.PrevU = x.PrevU;
		vertex.PrevV = z.PrevV;
		vertex.SurfaceOptions = WallLayout[i].TopLeft ? surfaceTopLeft : surfaceOther;
		vertex.LightLevelAdd = lightLevelAddAndMapId;
		vertex.RenderOptions = renderOptions;
	}
}

// UZDoom: lightmaps (plan M3.2). Like hw_walls.cpp SetWallCoordinates for a full line wall: the corners take the four TexCoords of the
// side's LightmapSurface (HWWall order LOLFT, UPLFT, UPRGT, LORGT); LightmapIndex = LightmapNum, < 0 = no lightmap.
// Slices of 3D floors are not lit by wall lightmaps.
void GeometryRenderer::SetWallLightmap(DynamicVertex* data, side_t* side, WallLocation location, bool twoSided)
{
	if (side == nullptr || side->lightmap == nullptr)
		return;

	// side_t::lightmap: top, M1S, M2S, bottom, then 3D floors (maploader.cpp SetSideLightmap)
	int index = location == WallLocation::Upper ? 0 : location == WallLocation::Lower ? 3 : twoSided ? 2 : 1;
	const LightmapSurface& surface = side->lightmap[index];
	if (surface.Type == ST_NULL || surface.TexCoords == nullptr)
		return;

	const float* uv = surface.TexCoords;
	for (int i = 0; i < 6; i++)
	{
		bool left = WallLayout[i].XCorner == 0;
		bool top = WallLayout[i].ZCorner == 0;
		int corner = left ? (top ? 1 : 0) : (top ? 2 : 3);
		data[i].LightmapU = uv[corner * 2];
		data[i].LightmapV = uv[corner * 2 + 1];
		data[i].LightmapIndex = (float)surface.LightmapNum;
	}
}

void GeometryRenderer::SetWallVertices(DynamicVertexArray& data, const WallVertices& wv, int lightLevelAdd, int lightIndex, int overrideLightIndex, int wallLightLevel,
	int mapId, WallLocation location, int textureIndex, float alpha, int addAlpha)
{
	SetWallVertices(data.data(), wv, lightLevelAdd, lightIndex, overrideLightIndex, wallLightLevel, mapId, location, textureIndex, alpha, addAlpha);
}

std::unique_ptr<DynamicVertexArray> GeometryRenderer::GetWallVertices(const WallVertices& wv, int lightLevelAdd, int lightIndex, int overrideLightIndex, int wallLightLevel,
	int mapId, WallLocation location, int textureIndex, float alpha, int addAlpha)
{
	// Helion: WorldStatic.DataCache.GetWallVertices() hands out a pooled array of 6 vertices.
	auto data = std::make_unique<DynamicVertexArray>(6);
	SetWallVertices(data->data(), wv, lightLevelAdd, lightIndex, overrideLightIndex, wallLightLevel, mapId, location, textureIndex, alpha, addAlpha);
	return data;
}

void GeometryRenderer::GetFlatVertices(DynamicVertexArray& vertices, int startIndex, const TriangulatedWorldVertex& root, const TriangulatedWorldVertex& second,
	const TriangulatedWorldVertex& third, int lightIndex, int overrideLightIndex, int flatLightLevel, int upper, int lower, int addAlpha, float alpha, int textureIndex)
{
	float surfaceOptions = VertexOptions::PackSurface(0, alpha, addAlpha, upper, lower, overrideLightIndex, textureIndex);
	float renderOptions = VertexOptions::PackRender(lightIndex, flatLightLevel);
	const TriangulatedWorldVertex* corners[3] = { &root, &second, &third };
	DynamicVertex* vertex = &vertices[startIndex];
	for (int i = 0; i < 3; i++, vertex++)
	{
		const TriangulatedWorldVertex& c = *corners[i];
		vertex->X = c.X;
		vertex->Y = c.Y;
		vertex->Z = c.Z;
		vertex->PrevX = c.X;
		vertex->PrevY = c.Y;
		vertex->PrevZ = c.PrevZ;
		vertex->U = c.U;
		vertex->V = c.V;
		vertex->PrevU = c.PrevU;
		vertex->PrevV = c.PrevV;
		vertex->SurfaceOptions = surfaceOptions;
		// Helion leaves LightLevelAdd of flat vertices at its default (0).
		vertex->LightLevelAdd = 0;
		vertex->RenderOptions = renderOptions;
	}
}

// Helion: wall.LightLevelAbsolute ? wall.LightLevel : clamp(wall.LightLevel + side.LightLevel, 0, 255).
// UZDoom: the wall light of a tier is side_t::TierLights[tier] (WALLF_ABSLIGHTING_<tier>: absolute, only the tier) or, like
// side_t::GetLightLevel, Light + TierLights[tier] (relative, or WALLF_ABSLIGHTING with the whole side absolute). Whether the light is
// absolute is decided by GetLightBufferIndex (override light index = dark index); r_fakecontrast is the separate light level add
// (GetLightLevelAdd), as Helion keeps the contrast out of the wall light byte. Relative light can be negative (darker than the
// sector), so the value is signed (-256..255, VertexOptions::PackRender); the shader clamps the sum to 0..256.
int GeometryRenderer::GetWallLightLevel(side_t* side, WallLocation wall)
{
	int which = wall == WallLocation::Upper ? side_t::top : wall == WallLocation::Lower ? side_t::bottom : side_t::mid;

	int light;
	if (side->Flags & (WALLF_ABSLIGHTING_TIER << which))
		light = side->TierLights[which];
	else if (side->Flags & WALLF_ABSLIGHTING)
		light = side->Light + side->TierLights[which];
	else
	{
		// side_t::GetLightLevel: relative side light is not applied in foggy sectors unless WALLF_LIGHT_FOG.
		sector_t* sector = side->sector;
		bool foggy = !sector->Colormap.FadeColor.isBlack() || (sector->Level->flags & LEVEL_HASFADETABLE);
		light = (!foggy || (side->Flags & WALLF_LIGHT_FOG)) ? side->Light + side->TierLights[which] : 0;
	}

	return std::clamp(light, -256, 255);
}

void GeometryRenderer::ReleaseUnmanagedResources()
{
	// Helion: m_staticCacheGeometryRenderer?.Dispose(); m_skyRenderer?.Dispose(); Portals?.Dispose();
	// C++ disposes by destruction; the raw pointers into the owners are cleared with them.
	m_staticCacheGeometryRenderer = nullptr;
	m_staticCacheOwner.reset();
	m_skyRenderer.reset();
	m_portals = nullptr;
	m_portalsOwner.reset();
}

void GeometryRenderer::CheckInvalidatedSideCounter(int& sideCounter, int geometryCounter)
{
	if (sideCounter != geometryCounter)
	{
		sideCounter = geometryCounter;
		m_sectorChangedLine = true;
	}
}

bool GeometryRenderer::CheckInvalidatedFlatCounter(int& flatCounter, int geometryCounter)
{
	if (flatCounter != geometryCounter)
	{
		flatCounter = geometryCounter;
		return true;
	}

	return false;
}

// UZDoom: slopes. Helion walls have one Z per top/bottom edge; the corners of the wall that are bounded by a sloped plane take the Z of that
// plane at their own X/Y. Vertices are in the 6 per wall layout of SetWallVertices: even indices are top, odd indices are bottom.
// V moves with Z at the wall's texture scale (texel rows per map unit of the left / right edge) so the texture is not stretched,
// like hw_walls.cpp computes the texture coordinates of each corner from its own height. PrevZ/PrevV move by the same amount.
void GeometryRenderer::ApplyWallSlopes(DynamicVertex* vertices, int count, const RenderPlane* topPlane, const RenderPlane* bottomPlane)
{
	const secplane_t* top = nullptr;
	const secplane_t* bottom = nullptr;

	if (topPlane != nullptr && topPlane->Sector != nullptr && topPlane->Plane().isSlope())
		top = &topPlane->Plane();
	if (bottomPlane != nullptr && bottomPlane->Sector != nullptr && bottomPlane->Plane().isSlope())
		bottom = &bottomPlane->Plane();

	if (top == nullptr && bottom == nullptr)
		return;

	for (int wall = 0; wall + 6 <= count; wall += 6)
	{
		DynamicVertex* v = vertices + wall;

		// V per map unit of Z at the left (vertices 0/1) and right (2/3) edge, before any corner moves.
		auto vPerZ = [](const DynamicVertex& t, const DynamicVertex& b, bool prev)
		{
			float dz = prev ? t.PrevZ - b.PrevZ : t.Z - b.Z;
			float dv = prev ? b.PrevV - t.PrevV : b.V - t.V;
			return dz != 0 ? dv / dz : 0.0f;
		};
		const float edgeScale[2] = { vPerZ(v[0], v[1], false), vPerZ(v[2], v[3], false) };
		const float edgePrevScale[2] = { vPerZ(v[0], v[1], true), vPerZ(v[2], v[3], true) };

		for (int i = 0; i < 6; i++)
		{
			const secplane_t* plane = (i % 2) == 0 ? top : bottom;
			if (plane == nullptr)
				continue;

			DynamicVertex& vertex = v[i];
			const int xCorner = WallLayout[i].XCorner;
			float z = (float)plane->ZatPoint((double)vertex.X, (double)vertex.Y);
			float dz = z - vertex.Z;
			vertex.Z = z;
			vertex.PrevZ += dz;
			vertex.V -= dz * edgeScale[xCorner];
			vertex.PrevV -= dz * edgePrevScale[xCorner];
		}
	}
}

void GeometryRenderer::ApplySliceSlopes(DynamicVertex* vertices, int count, const RenderPlane* topPlane, const RenderPlane* bottomPlane)
{
	auto sloped = [](const RenderPlane* plane) { return plane != nullptr && plane->Sector != nullptr && plane->Plane().isSlope() ? plane : nullptr; };
	const RenderPlane* planes[2] = { sloped(topPlane), sloped(bottomPlane) };
	if (planes[0] == nullptr && planes[1] == nullptr)
		return;

	for (int wall = 0; wall + 6 <= count; wall += 6)
	{
		DynamicVertex* v = vertices + wall;
		auto vPerZ = [](const DynamicVertex& t, const DynamicVertex& b, bool prev)
		{
			float dz = prev ? t.PrevZ - b.PrevZ : t.Z - b.Z;
			float dv = prev ? b.PrevV - t.PrevV : b.V - t.V;
			return dz != 0 ? dv / dz : 0.0f;
		};
		const float edgeScale[2] = { vPerZ(v[0], v[1], false), vPerZ(v[2], v[3], false) };
		const float edgePrevScale[2] = { vPerZ(v[0], v[1], true), vPerZ(v[2], v[3], true) };

		for (int i = 0; i < 6; i++)
		{
			const RenderPlane* plane = planes[i % 2];
			DynamicVertex& vertex = v[i];
			// A corner clamped to another height than the plane's (opening, sky) is not on the plane.
			if (plane == nullptr || std::abs(vertex.Z - (float)plane->Z) > 0.01f)
				continue;

			const int xCorner = WallLayout[i].XCorner;
			float z = (float)plane->Plane().ZatPoint((double)vertex.X, (double)vertex.Y);
			float dz = z - vertex.Z;
			vertex.Z = z;
			vertex.PrevZ += dz;
			vertex.V -= dz * edgeScale[xCorner];
			vertex.PrevV -= dz * edgePrevScale[xCorner];
		}
	}
}

void GeometryRenderer::ClipWallTop(DynamicVertex* vertices, int count, const RenderPlane& clipPlane)
{
	const secplane_t* plane = clipPlane.Sector != nullptr ? &clipPlane.Plane() : nullptr;
	for (int wall = 0; wall + 6 <= count; wall += 6)
	{
		DynamicVertex* v = vertices + wall;
		auto vPerZ = [](const DynamicVertex& t, const DynamicVertex& b, bool prev)
		{
			float dz = prev ? t.PrevZ - b.PrevZ : t.Z - b.Z;
			float dv = prev ? b.PrevV - t.PrevV : b.V - t.V;
			return dz != 0 ? dv / dz : 0.0f;
		};
		const float edgeScale[2] = { vPerZ(v[0], v[1], false), vPerZ(v[2], v[3], false) };
		const float edgePrevScale[2] = { vPerZ(v[0], v[1], true), vPerZ(v[2], v[3], true) };

		for (int i = 0; i < 6; i += 2)
		{
			DynamicVertex& vertex = v[i];
			const int xCorner = WallLayout[i].XCorner;
			const double planeZ = plane != nullptr ? plane->ZatPoint((double)vertex.X, (double)vertex.Y) : clipPlane.Z;
			const float z = (float)planeZ;
			const float prevZ = (float)(planeZ + clipPlane.PrevZ - clipPlane.Z);
			if (vertex.Z > z)
			{
				vertex.V -= (z - vertex.Z) * edgeScale[xCorner];
				vertex.Z = z;
			}
			if (vertex.PrevZ > prevZ)
			{
				vertex.PrevV -= (prevZ - vertex.PrevZ) * edgePrevScale[xCorner];
				vertex.PrevZ = prevZ;
			}
		}
	}
}



}
