/*
** StaticCacheGeometryRenderer.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Static.StaticCacheGeometryRenderer.
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

#include "StaticCacheGeometryRenderer.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include "doomdata.h"
#include "g_levellocals.h"
#include "r_defs.h"
#include "p_3dfloors.h"
#include "r_sky.h"
#include "BindTextures.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderWorldData.h"
#include "HelionCVars.h"
#include "SkyGeometryManager.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/CoverWallUtil.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/FloodFill/FloodFillInput.h"
#include "World/WorldStatic.h"
#include "World/Static/StaticDataApplier.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalDriver.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

using ::Helion::World::Geometry::Sectors::GetRenderSector;
using ::Helion::World::Geometry::Sectors::GetBackRenderSector;
using ::Helion::World::Static::MoveState;
using ::Helion::World::Static::RenderSectorState;
using ::Helion::World::Static::RenderSideState;
using ::Helion::World::Static::RenderStaticState;
using ::Helion::World::Static::StaticDataApplier;
using ::Helion::World::WorldStatic;
using Portals::FloodFill::FloodFillInput;
using Portals::FloodSet;

namespace
{

std::span<DynamicVertex> SpanOf(DynamicVertexArray* array)
{
	return array == nullptr ? std::span<DynamicVertex>() : std::span<DynamicVertex>(*array);
}

side_t* PartnerSide(const side_t* side)
{
	line_t* line = side->linedef;
	return line->sidedef[line->sidedef[0] == side ? 1 : 0];
}

bool IsFrontSide(const side_t* side)
{
	return side->linedef->sidedef[0] == side;
}

bool IsPolyobjectSide(const side_t* side)
{
	return (side->renderDynamic & SD_Polyobject) != 0 || (side->Flags & WALLF_POLYOBJ) != 0;
}

}

StaticCacheGeometryRenderer::StaticCacheGeometryRenderer(Texture::Legacy::LegacyGLTextureManager& textureManager,
	::Helion::Render::OpenGL::Shader::RenderProgram& program, GeometryRenderer& geometryRenderer)
	: m_textureManager(textureManager), m_geometryRenderer(geometryRenderer), m_floodFillRenderer(&geometryRenderer.Portals().GetStaticFloodFillRenderer()),
	m_program(program), m_textureToGeometryLookup(textureManager), m_freeManager(textureManager)
{
	m_skyRenderer = std::make_unique<Sky::LegacySkyRenderer>(&textureManager);
	m_renderCoverWallAction = [this](side_t* side, DynamicVertexSpan vertices, WallLocation location, bool oneSided) { AddOrUpdateCoverWall(side, vertices, location, oneSided); };
	m_renderSectorWallVertices3D = [this](side_t* side, WallLocation wall, const RenderSector& wallSector, GLLegacyTexture* texture, DynamicVertexSpan vertices,
		Sector3D* sector3D) { RenderSectorWallVertices3D(side, wall, wallSector, texture, vertices, sector3D); };
}

StaticCacheGeometryRenderer::~StaticCacheGeometryRenderer()
{
	Dispose();
}

int StaticCacheGeometryRenderer::TransparentGeometryCompare(const GeometryData& x, const GeometryData& y)
{
	if (x.Texture->TransparentPixelCount == y.Texture->TransparentPixelCount)
		return x.Texture->TextureId < y.Texture->TextureId ? -1 : (x.Texture->TextureId > y.Texture->TextureId ? 1 : 0);

	return x.Texture->TransparentPixelCount < y.Texture->TransparentPixelCount ? -1 : 1;
}

void StaticCacheGeometryRenderer::Subscribe()
{
	using namespace ::Helion::World::SectorUpdates;
	m_tokenMoveStart = SectorMoveStartEvent.Subscribe([this](sector_t* sector, int pos) { World_SectorMoveStart(sector, pos); });
	m_tokenMoveComplete = SectorMoveCompleteEvent.Subscribe([this](sector_t* sector, int pos) { World_SectorMoveComplete(sector, pos); });
	m_tokenTransformed = PlaneTransformedEvent.Subscribe([this](sector_t* sector, int pos) { World_SectorPlaneTransformed(sector, pos); });
	m_tokenPlaneTexture = PlaneTextureChangedEvent.Subscribe([this](sector_t* sector, int pos, FTextureID textureId, FTextureID previousTextureId)
		{ World_PlaneTextureChanged(sector, pos, textureId, previousTextureId); });
	m_tokenSideTexture = SideTextureChangedEvent.Subscribe([this](side_t* side, int tier, FTextureID textureId, FTextureID previousTextureId)
		{ World_SideTextureChanged(side, tier, textureId, previousTextureId); });
}

void StaticCacheGeometryRenderer::Unsubscribe()
{
	using namespace ::Helion::World::SectorUpdates;
	if (m_tokenMoveStart != 0) SectorMoveStartEvent.Unsubscribe(m_tokenMoveStart);
	if (m_tokenMoveComplete != 0) SectorMoveCompleteEvent.Unsubscribe(m_tokenMoveComplete);
	if (m_tokenTransformed != 0) PlaneTransformedEvent.Unsubscribe(m_tokenTransformed);
	if (m_tokenPlaneTexture != 0) PlaneTextureChangedEvent.Unsubscribe(m_tokenPlaneTexture);
	if (m_tokenSideTexture != 0) SideTextureChangedEvent.Unsubscribe(m_tokenSideTexture);
	m_tokenMoveStart = m_tokenMoveComplete = m_tokenTransformed = m_tokenPlaneTexture = m_tokenSideTexture = 0;
}

void StaticCacheGeometryRenderer::UpdateTo(FLevelLocals* world, bool sameAsPreviousMap)
{
	ClearData(world, sameAsPreviousMap);

	m_world = world;
	m_worldReload = true;
	m_vanillaRender = gl_vanillarender;

	// Helion StaticDataApplier.DetermineStaticData (the flood fill part)
	FloodFillInput::DetermineFloodFill(world);

	Subscribe();

	m_geometryRenderer.SetInitRender();

	if (!sameAsPreviousMap)
		m_skyRenderer->Reset();

	SetupCoverGeometry(world, sameAsPreviousMap);

	for (unsigned i = 0; i < world->sectors.Size(); i++)
	{
		sector_t* sector = &world->sectors[i];
		for (auto& sector3D : m_geometryRenderer.Sector3DTable().Sectors3D(sector))
			m_taggedSectors3D[sector3D->ControlSector->Index()].push_back(sector3D.get());
	}

	for (unsigned i = 0; i < world->sectors.Size(); i++)
	{
		sector_t* sector = &world->sectors[i];
		AddTransferSector(sector);

		if ((sector->renderDynamic[sector_t::floor] & IgnoreFlags) == 0)
			AddSectorPlane(sector, SectorPlaneFace::Floor, true);
		if ((sector->renderDynamic[sector_t::ceiling] & IgnoreFlags) == 0)
			AddSectorPlane(sector, SectorPlaneFace::Ceiling, false);
	}

	for (unsigned i = 0; i < world->sectors.Size(); i++)
	{
		sector_t* sector = &world->sectors[i];
		if (m_geometryRenderer.Sector3DTable().HasSectors3D(sector))
			AddSectors3D(sector, false);
	}

	for (unsigned i = 0; i < world->lines.Size(); i++)
		AddLine(&world->lines[i]);

	for (auto& list : m_geometry.GetAllGeometry())
	{
		for (auto& data : list)
		{
			data->Pipeline->Bind(true);
			data->Vbo->UploadIfNeeded();
		}
	}

	m_worldReload = false;

	InitSignatures();
}

void StaticCacheGeometryRenderer::World_SectorFogColorChanged(sector_t* sector, uint32_t previousFade)
{
	uint32_t currentFade = sector->Colormap.FadeColor.d & 0xffffff;
	previousFade &= 0xffffff;
	if ((previousFade == 0 && currentFade == 0) || (previousFade != 0 && currentFade != 0))
		return;

	for (unsigned i = 0; i < sector->Lines.Size(); i++)
	{
		line_t* line = sector->Lines[i];
		if (line->sidedef[1] == nullptr)
			continue;

		AddTwoSided(line->sidedef[0], true, true, true);
		AddTwoSided(line->sidedef[1], false, true, true);
	}
}

void StaticCacheGeometryRenderer::Render(Data::RenderDataStyle style)
{
	Render(ToGeometryType(style));
}

bool StaticCacheGeometryRenderer::HasStyleToRender(Data::RenderDataStyle style)
{
	return m_geometry.GetGeometry(ToGeometryType(style)).size() > 0;
}

// UZDoom: besides Helion's texture transform setters this is raised with pos -1 from sector_t::SetAllVerticesDirty, which
// every plane mover calls on every tick. Only planes whose values differ from the cached signature are rebuilt then.
void StaticCacheGeometryRenderer::World_SectorPlaneTransformed(sector_t* sector, int pos)
{
	int first = pos < 0 ? 0 : pos;
	int last = pos < 0 ? 1 : pos;
	for (int i = first; i <= last; i++)
	{
		// A mover is going to rebuild the plane when it completes; scrolling planes are dynamic.
		if ((sector->renderDynamic[i] & (SD_Movement | SD_Scroll)) != 0)
			continue;

		if (pos < 0 && !PlaneSignatureChanged(sector, i))
			continue;

		// The other plane is moving: its mover rebuilds the lines when it completes. Running the line part of the move
		// start / complete handlers here would mark the lines processed for this tick and make the mover's own
		// SectorMoveComplete skip them, leaving the walls built for the last static heights.
		if (StaticDataApplier::IsMoving(sector))
		{
			RefreshStaticPlane(sector, i);
			continue;
		}

		HandleSectorMoveStart(sector, i);
		HandleSectorMoveComplete(sector, i);
	}
}

void StaticCacheGeometryRenderer::RefreshStaticPlane(sector_t* sector, int pos)
{
	RefreshPlaneSignature(sector, pos);
	bool floor = pos == sector_t::floor;
	SectorPlaneFace face = floor ? SectorPlaneFace::Floor : SectorPlaneFace::Ceiling;
	m_geometryRenderer.SetBuffer(false);
	m_geometryRenderer.SetRenderMode(GeometryRenderMode::Dynamic, TransferHeightView::Middle);
	RenderPlane plane = RenderPlane::From(sector, face);
	if (floor)
		m_geometryRenderer.SetRenderFloor(plane);
	else
		m_geometryRenderer.SetRenderCeiling(plane);
	AddSectorPlane(sector, face, floor, true);
}

void StaticCacheGeometryRenderer::SetupCoverGeometry(FLevelLocals* world, bool sameAsPreviousMap)
{
	GLLegacyTexture* texture = m_textureManager.WhiteTexture;
	int textureIndex = 0;

	// Cover flat geometry is always allocated to ensure sprites are covered/clipped to transfer heights
	if (!sameAsPreviousMap)
	{
		FString label = "CoverFlat";
		m_coverFlatGeometry = CreateGeometryData(GeometryType::Flat, textureIndex, true, 0, texture, &label);
	}

	if (!m_vanillaRender)
	{
		m_coverWallGeometry.reset();
		m_coverWallGeometryOneSided.reset();
		return;
	}

	if (!sameAsPreviousMap || (sameAsPreviousMap && m_coverWallGeometry == nullptr))
	{
		int oneSided = 0;
		for (unsigned i = 0; i < world->lines.Size(); i++)
		{
			if (world->lines[i].sidedef[1] == nullptr)
				oneSided++;
		}

		int sidesWithTextures = 0;
		for (unsigned i = 0; i < world->sides.Size(); i++)
		{
			const side_t& side = world->sides[i];
			if (side.textures[side_t::top].texture.GetIndex() > 0 || side.textures[side_t::bottom].texture.GetIndex() > 0)
				sidesWithTextures++;
		}

		FString twoSidedLabel = "CoverWall Two-Sided";
		FString oneSidedLabel = "CoverWall One-Sided";
		m_coverWallGeometry = CreateGeometryData(GeometryType::Wall, textureIndex, true, sidesWithTextures * WallVertices, texture, &twoSidedLabel);
		m_coverWallGeometryOneSided = CreateGeometryData(GeometryType::Wall, textureIndex, true, oneSided * WallVertices, texture, &oneSidedLabel);
	}
}

void StaticCacheGeometryRenderer::UpdateSectorPlaneFloodFill(line_t* line)
{
	UpdateSectorPlaneFloodFill(line->sidedef[0], GetRenderSector(line->sidedef[0]->sector, TransferHeightView::Middle), true);
	if (line->sidedef[1] != nullptr)
		UpdateSectorPlaneFloodFill(line->sidedef[1], GetRenderSector(line->sidedef[1]->sector, TransferHeightView::Middle), false);
}

void StaticCacheGeometryRenderer::UpdateSectorPlaneFloodFill(side_t* facingSide, const RenderSector& facingSector, bool isFront)
{
	auto& state = RenderStaticState::Side(facingSide);
	if (state.FloorFloodKey > 0)
		m_geometryRenderer.Portals().UpdateFloodFillPlane(facingSide, facingSector, SectorPlanes::Floor, SectorPlaneFace::Floor, isFront, m_floodFillRenderer);
	if (state.CeilingFloodKey > 0)
		m_geometryRenderer.Portals().UpdateFloodFillPlane(facingSide, facingSector, SectorPlanes::Ceiling, SectorPlaneFace::Ceiling, isFront, m_floodFillRenderer);
}

void StaticCacheGeometryRenderer::CheckForFloodFill(side_t* facingSide, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFront)
{
	SideTexture previous = RenderStaticState::Side(facingSide).FloodTextures;
	FloodFillInput::SetFloodFillSide(facingSide, otherSide, facingSector, otherSector);
	if (previous == RenderStaticState::Side(facingSide).FloodTextures)
		return;

	UpdateFloodFillSideState(facingSide, otherSide, otherSector, isFront, previous, SideTexture::Upper);
	UpdateFloodFillSideState(facingSide, otherSide, otherSector, isFront, previous, SideTexture::Lower);
}

void StaticCacheGeometryRenderer::UpdateFloodFillSideState(side_t* facingSide, side_t* otherSide, const RenderSector& otherSector, bool isFront,
	SideTexture previous, SideTexture sideTexture)
{
	bool isUpper = (sideTexture & SideTexture::Upper) != SideTexture::None;
	auto& state = RenderStaticState::Side(facingSide);
	::Helion::World::Static::FloodKeys& floodKeys = isUpper ? state.UpperFloodKeys : state.LowerFloodKeys;

	if ((previous & sideTexture) == SideTexture::None)
	{
		if ((state.FloodTextures & sideTexture) != SideTexture::None && floodKeys.Key1 == 0)
			m_geometryRenderer.Portals().AddStaticFloodFillSide(facingSide, otherSide, otherSector, sideTexture, isFront, m_floodFillRenderer);
		return;
	}

	if ((state.FloodTextures & sideTexture) == SideTexture::None && floodKeys.Key1 != 0)
	{
		if (floodKeys.Key1 > 0)
			m_floodFillRenderer->ClearStaticWall(floodKeys.Key1);
		if (floodKeys.Key2 > 0)
			m_floodFillRenderer->ClearStaticWall(floodKeys.Key2);

		floodKeys = ::Helion::World::Static::FloodKeys();
	}
}

void StaticCacheGeometryRenderer::AddTransferSector(sector_t* sector)
{
	sector_t* control = sector->GetHeightSec();
	if (control == nullptr)
		return;

	m_transferHeightsLookup[control->Index()].push_back(sector);
}

void StaticCacheGeometryRenderer::AddLine(line_t* line, bool update)
{
	// Polyobject sides are always dynamic (plan D27).
	if (IsPolyobjectSide(line->sidedef[0]))
		return;

	if (line->sidedef[1] != nullptr)
	{
		AddTwoSided(line->sidedef[0], true, update);
		AddTwoSided(line->sidedef[1], false, update);
		return;
	}

	AddOneSided(line->sidedef[0], true, update);
}

void StaticCacheGeometryRenderer::AddOneSided(side_t* side, bool isFrontSide, bool update)
{
	sector_t* sector = side->sector;
	bool dynamic = side->renderDynamic != 0 || StaticDataApplier::IsMoving(sector);
	if (dynamic && (sector->renderDynamic[sector_t::floor] == SD_Movement || sector->renderDynamic[sector_t::ceiling] == SD_Movement))
		return;

	WallRef wall = MakeWall(side, WallLocation::Middle);

	if (m_geometryRenderer.Sector3DTable().HasSectors3D(sector))
	{
		m_geometryRenderer.SetRenderOneSided(side);
		auto& planes3D = m_geometryRenderer.Sector3DTable().SectorPlanes3D(sector);
		RenderWallSliceResult result = m_geometryRenderer.RenderWallSlices3D(side, WallLocation::Middle, isFrontSide, side, sector, sector,
			std::span<::Helion::World::Geometry::Sectors::SectorPlane3D>(planes3D), m_geometryRenderer.OneSidedSliceFunc());
		AddSkyGeometry(side, WallLocation::Middle, nullptr, result.SkyVertices, sector, update);

		if (result.Vertices.size() > 0)
		{
			AddFloodFillPlane(side, sector, true);
			UpdateVertices(*wall.Static, wall.TextureHandle, result.Vertices, nullptr, side, WallLocation::Middle, true, result.Texture);
		}
		return;
	}

	DynamicVertexArray* sideVertices = nullptr;
	SkyVertexArray* skyVertices = nullptr;
	GLLegacyTexture* texture = nullptr;
	m_geometryRenderer.RenderOneSided(side, isFrontSide, sideVertices, skyVertices, texture);

	AddSkyGeometry(side, WallLocation::Middle, nullptr, skyVertices, sector, update);

	if (sideVertices != nullptr)
	{
		AddFloodFillPlane(side, sector, true);
		UpdateVertices(*wall.Static, wall.TextureHandle, SpanOf(sideVertices), nullptr, side, WallLocation::Middle, true, texture, nullptr, true);
	}
}

void StaticCacheGeometryRenderer::AddFloodFillPlane(side_t* side, const RenderSector& sector, bool isFrontSide)
{
	auto& state = RenderStaticState::Side(side);
	bool flood = RenderStaticState::SectorById(sector.Id).Flood;
	if (!flood && state.MidTextureFlood == SectorPlanes::None)
		return;

	side_t* partner = PartnerSide(side);
	if (partner != nullptr && side->sector == partner->sector)
		return;

	bool floodFloor = (flood && !sector.Floor.MidTextureHack) || state.MidTextureFlood != SectorPlanes::None;
	bool floodCeiling = (flood && !sector.Ceiling.MidTextureHack) || state.MidTextureFlood != SectorPlanes::None;

	bool skyHack = false;
	if (partner != nullptr)
		GeometryRenderer::UpperOrSkySideIsVisible(side, RenderSector(side->sector), RenderSector(partner->sector), skyHack);

	if (floodFloor && state.FloorFloodKey == 0)
	{
		if (!IsSkyTexture(sector.Floor.TextureHandle))
		{
			m_geometryRenderer.Portals().AddFloodFillPlane(side, sector, SectorPlanes::Floor, SectorPlaneFace::Floor, isFrontSide, m_floodFillRenderer);
		}
		else
		{
			SkyVertexArray* renderedSkyVertices = nullptr;
			m_geometryRenderer.RenderSkySide(side, sector, SectorPlaneFace::Floor, isFrontSide, renderedSkyVertices);
			AddSkyGeometry(side, WallLocation::Lower, nullptr, renderedSkyVertices, sector, false);
		}
	}

	// Sky ceilings are handled differently
	if (floodCeiling && !skyHack && state.CeilingFloodKey == 0 && !IsSkyTexture(sector.Ceiling.TextureHandle) &&
		(partner == nullptr || !IsSkyTexture(partner->sector->GetTexture(sector_t::ceiling).GetIndex())))
	{
		m_geometryRenderer.Portals().AddFloodFillPlane(side, sector, SectorPlanes::Ceiling, SectorPlaneFace::Ceiling, isFrontSide, m_floodFillRenderer);
	}
}

void StaticCacheGeometryRenderer::AddTwoSided(side_t* side, bool isFrontSide, bool update, bool fogBarrierOnly)
{
	side_t* otherSide = PartnerSide(side);
	if (update && (StaticDataApplier::IsMoving(side->sector) || StaticDataApplier::IsMoving(otherSide->sector)))
		return;

	RenderSector facingSector = GetRenderSector(side->sector, TransferHeightView::Middle);
	RenderSector otherSector = GetBackRenderSector(otherSide->sector, TransferHeightView::Middle);

	bool floorDynamic = (side->sector->renderDynamic[sector_t::floor] & SD_Movement) != 0 || (otherSide->sector->renderDynamic[sector_t::floor] & SD_Movement) != 0;
	bool ceilingDynamic = (side->sector->renderDynamic[sector_t::ceiling] & SD_Movement) != 0 || (otherSide->sector->renderDynamic[sector_t::ceiling] & SD_Movement) != 0;
	bool sideDynamic = side->renderDynamic != 0;
	bool upper = !(ceilingDynamic && sideDynamic);
	bool lower = !(floorDynamic && sideDynamic);
	bool middle = !((floorDynamic || ceilingDynamic) && sideDynamic);

	m_geometryRenderer.SetRenderTwoSided(side, otherSide);

	if (fogBarrierOnly)
	{
		if (middle)
			UpdateFogBarrier(side, otherSide, facingSector, otherSector, isFrontSide);
		return;
	}

	auto& state = RenderStaticState::Side(side);

	if (middle && m_geometryRenderer.ShouldRenderFogBarrier(side, otherSide))
	{
		DynamicVertexArray* sideVertices = nullptr;
		m_geometryRenderer.RenderFogBarrier(side, otherSide, facingSector, otherSector, isFrontSide, sideVertices);
		state.HasFog = true;
		SetSideVertices(MakeFogWall(side), update, SpanOf(sideVertices), true, true, nullptr);
	}

	AddFloodFillPlane(side, facingSector, isFrontSide);

	bool skyHack;
	bool upperVisible = GeometryRenderer::UpperIsVisibleOrFlood(side, otherSide, facingSector, otherSector, skyHack);
	bool hasSectors3D = m_geometryRenderer.Sector3DTable().HasSectors3D(side->sector);
	if (upper && upperVisible)
	{
		RenderWallSliceResult result;
		if (hasSectors3D)
		{
			auto& planes3D = m_geometryRenderer.Sector3DTable().SectorPlanes3D(side->sector);
			result = m_geometryRenderer.RenderWallSlices3D(side, WallLocation::Upper, isFrontSide, otherSide, facingSector, otherSector,
				std::span<::Helion::World::Geometry::Sectors::SectorPlane3D>(planes3D), m_geometryRenderer.TwoSidedUpperSliceFunc());
		}
		else
		{
			DynamicVertexArray* sideVertices = nullptr;
			SkyVertexArray* skyVertices = nullptr;
			SkyVertexArray* skyVertices2 = nullptr;
			m_geometryRenderer.RenderTwoSidedUpper(side, otherSide, facingSector, otherSector, isFrontSide, sideVertices, skyVertices, skyVertices2);
			result = RenderWallSliceResult(sideVertices, skyVertices, nullptr, skyVertices2);
		}

		// TODO this is dumb
		if (result.SkyVertices2 != nullptr)
		{
			// The side has to be marked to be re-calculated on movement because it can completely change how the sky is rendered.
			state.UpperSky = true;
			result.SkyVertices = result.SkyVertices2;
		}

		SetSideVertices(MakeWall(side, WallLocation::Upper), update, result.Vertices, upperVisible, true, nullptr);
		// Sky hack and skyVertices2 are done from the facingSector, otherwise use the otherSector like normal.
		// Required for id24 flat mapping using different floor/ceiling textures.
		AddSkyGeometry(side, WallLocation::Upper, nullptr, result.SkyVertices, skyHack || result.SkyVertices2 != nullptr ? facingSector : otherSector, update);

		if (!update)
		{
			if ((state.FloodTextures & SideTexture::Upper) != SideTexture::None)
				m_geometryRenderer.Portals().AddStaticFloodFillSide(side, otherSide, otherSector, SideTexture::Upper, isFrontSide, m_floodFillRenderer);
		}

		if (m_vanillaRender && result.SkyVertices != nullptr)
		{
			DynamicVertexArray* sideVertices = m_geometryRenderer.RenderTwoSidedUpperOrLowerRaw(WallLocation::Upper, side, facingSector, otherSector, isFrontSide);
			AddOrUpdateCoverWall(side, SpanOf(sideVertices), WallLocation::Upper, false);
		}
	}

	bool lowerVisible = GeometryRenderer::LowerIsVisible(side, facingSector, otherSector);
	if (lower && lowerVisible)
	{
		RenderWallSliceResult result;
		if (hasSectors3D)
		{
			auto& planes3D = m_geometryRenderer.Sector3DTable().SectorPlanes3D(side->sector);
			result = m_geometryRenderer.RenderWallSlices3D(side, WallLocation::Lower, isFrontSide, otherSide, facingSector, otherSector,
				std::span<::Helion::World::Geometry::Sectors::SectorPlane3D>(planes3D), m_geometryRenderer.TwoSidedLowerSliceFunc(), nullptr, false);
		}
		else
		{
			DynamicVertexArray* sideVertices = nullptr;
			SkyVertexArray* skyVertices = nullptr;
			SkyVertexArray* skyVertices2 = nullptr;
			m_geometryRenderer.RenderTwoSidedLower(side, otherSide, facingSector, otherSector, isFrontSide, sideVertices, skyVertices, skyVertices2);
			result = RenderWallSliceResult(sideVertices, skyVertices, nullptr, skyVertices2);
		}

		if (result.SkyVertices2 != nullptr)
		{
			// The side has to be marked to be re-calculated on movement because it can completely change how the sky is rendered.
			state.LowerSky = true;
			result.SkyVertices = result.SkyVertices2;
		}

		SetSideVertices(MakeWall(side, WallLocation::Lower), update, result.Vertices, lowerVisible, true, nullptr);
		// UZDoom: the sky side of a closed other sector (RenderSkySide) is the facing sector's ceiling sky.
		const bool lowerSkySide = result.SkyVertices2 != nullptr;
		AddSkyGeometry(side, WallLocation::Lower, nullptr, result.SkyVertices, lowerSkySide ? facingSector : otherSector, update, lowerSkySide);

		if (!update && result.SkyVertices == nullptr)
		{
			if ((state.FloodTextures & SideTexture::Lower) != SideTexture::None)
				m_geometryRenderer.Portals().AddStaticFloodFillSide(side, otherSide, otherSector, SideTexture::Lower, isFrontSide, m_floodFillRenderer);
		}

		if (m_vanillaRender && result.SkyVertices != nullptr)
		{
			DynamicVertexArray* sideVertices = m_geometryRenderer.RenderTwoSidedUpperOrLowerRaw(WallLocation::Lower, side, facingSector, otherSector, isFrontSide);
			AddOrUpdateCoverWall(side, SpanOf(sideVertices), WallLocation::Lower, false);
		}
	}

	WallRef middleWall = MakeWall(side, WallLocation::Middle);
	if (middle && middleWall.TextureHandle != 0 && ShouldRenderStaticMiddle(side))
	{
		RenderWallSliceResult result;
		if (hasSectors3D)
		{
			auto& planes3D = m_geometryRenderer.Sector3DTable().SectorPlanes3D(side->sector);
			result = m_geometryRenderer.RenderWallSlices3D(side, WallLocation::Middle, isFrontSide, otherSide, facingSector, otherSector,
				std::span<::Helion::World::Geometry::Sectors::SectorPlane3D>(planes3D), m_geometryRenderer.TwoSidedMiddleSliceFunc());
		}
		else
		{
			DynamicVertexArray* sideVertices = nullptr;
			m_geometryRenderer.RenderTwoSidedMiddle(side, otherSide, facingSector, otherSector, isFrontSide, sideVertices);
			result = RenderWallSliceResult(sideVertices, nullptr, nullptr);
		}

		SetSideVertices(middleWall, update, result.Vertices, true, GetRepeatY(side, WallLocation::Middle), nullptr);

		SideTexture sideVisibility = SideTexture::None;
		if (upperVisible)
			sideVisibility |= SideTexture::Upper;
		if (lowerVisible)
			sideVisibility |= SideTexture::Lower;

		if (m_vanillaRender && result.Vertices.size() > 0)
			m_geometryRenderer.RenderMidTexCoverWalls(side, facingSector, otherSector, result.Vertices, sideVisibility, m_renderCoverWallAction);
	}
}

void StaticCacheGeometryRenderer::UpdateFogBarrier(side_t* side, side_t* otherSide, const RenderSector& facingSector, const RenderSector& otherSector, bool isFrontSide)
{
	auto& state = RenderStaticState::Side(side);
	bool shouldRenderFogBarrier = m_geometryRenderer.ShouldRenderFogBarrier(side, otherSide);
	if (shouldRenderFogBarrier && (!state.HasFog || state.FogCleared))
	{
		DynamicVertexArray* sideVertices = nullptr;
		m_geometryRenderer.RenderFogBarrier(side, otherSide, facingSector, otherSector, isFrontSide, sideVertices);
		state.HasFog = true;
		state.FogCleared = false;
		SetSideVertices(MakeFogWall(side), true, SpanOf(sideVertices), true, true, nullptr);
	}
	else if (!shouldRenderFogBarrier && state.HasFog)
	{
		state.FogCleared = true;
		ClearGeometryVertices(state.Fog);
	}
}

bool StaticCacheGeometryRenderer::ShouldRenderStaticMiddle(side_t* side)
{
	if ((side->renderDynamic & SD_Scroll) == 0)
		return true;

	int textureHandle = FloodFillInput::SideTextureHandle(side, side_t::mid);

	// Mid textures that have Y scrolling physically move so ignore in the static renderer.
	if (textureHandle != 0 && (side->renderDynamic & SD_ScrollY) != 0)
		return false;

	// If the texture has transparent pixels and scrolls then do not render statically.
	// Textures with no transparent pixels can be added for when the camera is outside the dynamic distance
	// that the static non-scrolling texture will be rendered in place.
	GLLegacyTexture* texture = m_textureManager.GetTexture(textureHandle, false);
	return texture->TransparentPixelCount == 0;
}

void StaticCacheGeometryRenderer::AddSkyGeometry(side_t* side, WallLocation wallLocation, const RenderPlane* plane, const SkyVertexArray* vertices,
	const RenderSector& sector, bool update, bool ceilingSky)
{
	if (vertices == nullptr)
		return;

	bool sideUpdated = side == nullptr;
	bool planeUpdated = plane == nullptr;

	if (update)
	{
		if (side != nullptr && SkyGeometryManager::HasSide(side))
		{
			sideUpdated = true;
			SkyGeometryManager::UpdateSide(side, wallLocation, *vertices);
		}

		if (plane != nullptr && SkyGeometryManager::HasPlane(plane->Id))
		{
			planeUpdated = true;
			SkyGeometryManager::UpdatePlane(plane->Id, *vertices);
		}

		if (sideUpdated && planeUpdated)
			return;
	}

	int skyPlane = sector_t::ceiling;
	if (plane != nullptr && plane->Facing == SectorPlaneFace::Floor)
		skyPlane = sector_t::floor;
	else if (side != nullptr && wallLocation == WallLocation::Lower && !ceilingSky)
		skyPlane = sector_t::floor;

	// UZDoom (plan M5.2): a sky plane with a sector portal (skybox) is drawn by the portal driver, not as level sky.
	if (sector.Sector != nullptr && Portals::PortalDriver::PlaneIsPortal(sector.Sector, skyPlane))
		return;

	Sky::ISkyComponent* sky = nullptr;
	if (!m_skyRenderer->GetOrCreateSectorSky(sector.Sector, skyPlane, sky))
		return;

	if (plane != nullptr && !planeUpdated)
	{
		SkyGeometryManager::AddPlane(sky, plane->Id, *vertices);
		return;
	}

	if (side == nullptr || sideUpdated)
		return;

	SkyGeometryManager::AddSide(sky, side, wallLocation, *vertices);
}

void StaticCacheGeometryRenderer::EnsureCapacity(TArray<StaticVertex>& data, int capacity)
{
	if ((int)data.Max() < capacity)
		data.Grow((unsigned)(capacity - (int)data.Size()));
}

void StaticCacheGeometryRenderer::AddVertices(TArray<StaticVertex>& staticVertices, DynamicVertexSpan vertices)
{
	unsigned staticStartIndex = staticVertices.Reserve((unsigned)vertices.size());
	for (size_t i = 0; i < vertices.size(); i++)
	{
		const DynamicVertex& v = vertices[i];
		StaticVertex& s = staticVertices[staticStartIndex + (unsigned)i];
		s = StaticVertex(v.X, v.Y, v.Z, v.U, v.V, v.SurfaceOptions, v.LightLevelAdd, v.RenderOptions);
		s.LightmapU = v.LightmapU;
		s.LightmapV = v.LightmapV;
		s.LightmapIndex = v.LightmapIndex;
	}
}

void StaticCacheGeometryRenderer::CopyVertices(StaticVertex* staticVertices, DynamicVertexSpan vertices, int index)
{
	for (size_t i = 0; i < vertices.size(); i++)
	{
		const DynamicVertex& v = vertices[i];
		StaticVertex& s = staticVertices[(size_t)index + i];
		s = StaticVertex(v.X, v.Y, v.Z, v.U, v.V, v.SurfaceOptions, v.LightLevelAdd, v.RenderOptions);
		s.LightmapU = v.LightmapU;
		s.LightmapV = v.LightmapV;
		s.LightmapIndex = v.LightmapIndex;
	}
}

int StaticCacheGeometryRenderer::TierOf(WallLocation location)
{
	switch (location)
	{
	case WallLocation::Upper: return side_t::top;
	case WallLocation::Lower: return side_t::bottom;
	default: return side_t::mid;
	}
}

StaticCacheGeometryRenderer::WallLocation StaticCacheGeometryRenderer::LocationOf(int tier)
{
	switch (tier)
	{
	case side_t::top: return WallLocation::Upper;
	case side_t::bottom: return WallLocation::Lower;
	default: return WallLocation::Middle;
	}
}

bool StaticCacheGeometryRenderer::GetRepeatY(const side_t* side, WallLocation location)
{
	if (location != WallLocation::Middle)
		return true;

	return (side->Flags & WALLF_WRAP_MIDTEX) != 0 || (side->linedef != nullptr && (side->linedef->flags & ML_WRAP_MIDTEX) != 0);
}

bool StaticCacheGeometryRenderer::IsSkyTexture(int textureHandle)
{
	return textureHandle == skyflatnum.GetIndex();
}

int StaticCacheGeometryRenderer::TextureHandleOf(const GLLegacyTexture* texture)
{
	if (texture == nullptr)
		return 0;

	const GLLegacyTexture* source = texture->ParentArrayTexture != nullptr && texture->Source == nullptr ? texture->ParentArrayTexture : texture;
	if (source->Source != nullptr)
		return source->Source->GetID().GetIndex();

	return source->TextureId;
}

StaticCacheGeometryRenderer::WallRef StaticCacheGeometryRenderer::MakeWall(side_t* side, WallLocation location)
{
	int tier = TierOf(location);
	return WallRef{ side, location, FloodFillInput::SideTextureHandle(side, tier), &RenderStaticState::Side(side).Static[tier], false };
}

StaticCacheGeometryRenderer::WallRef StaticCacheGeometryRenderer::MakeFogWall(side_t* side)
{
	// Helion: the fog wall uses the black texture. Texture handle 0 is the null / black texture of the texture manager.
	return WallRef{ m_geometryRenderer.FogSide(), WallLocation::Middle, 0, &RenderStaticState::Side(side).Fog, true };
}

void StaticCacheGeometryRenderer::SetSideVertices(const WallRef& wall, bool update, DynamicVertexSpan sideVertices, bool visible, bool repeatY, Sector3D* sector3D)
{
	if (sideVertices.size() == 0 || !visible)
		return;

	if (update)
	{
		UpdateVertices(*wall.Static, wall.TextureHandle, sideVertices, nullptr, wall.Side, wall.Location, repeatY, nullptr, nullptr, wall.AllowNullTexture);
		return;
	}

	GeometryType type = m_geometryRenderer.GetWallType(wall.Side, wall.Location, sector3D);
	if (m_vanillaRender && NeedsCoverWall(type))
		AddOrUpdateCoverWall(wall.Side, sideVertices, wall.Location, wall.Location == WallLocation::Middle);

	if (wall.TextureHandle <= 0 && !wall.AllowNullTexture)
		return;

	TArray<StaticVertex>& vertices = GetTextureVertices(type, wall.TextureHandle, repeatY);
	SetSideData(*wall.Static, type, wall.TextureHandle, (int)vertices.Size(), (int)sideVertices.size(), repeatY, nullptr);
	AddVertices(vertices, sideVertices);
}

void StaticCacheGeometryRenderer::SetSideData(StaticGeometryData& staticGeometry, GeometryType type, int textureHandle, int vboIndex, int vertexCount, bool repeatY,
	GeometryData* geometryData)
{
	if (geometryData == nullptr && !m_textureToGeometryLookup.TryGetValue(type, textureHandle, repeatY, geometryData))
		return;

	staticGeometry.GeometryData = geometryData;
	staticGeometry.Index = vboIndex;
	staticGeometry.Length = vertexCount;
}

TArray<StaticVertex>& StaticCacheGeometryRenderer::GetTextureVertices(GeometryType type, int textureHandle, bool repeatY)
{
	GeometryData* geometryData = nullptr;
	if (!m_textureToGeometryLookup.TryGetValue(type, textureHandle, repeatY, geometryData))
		geometryData = AllocateGeometryData(type, textureHandle, repeatY);

	return geometryData->Vbo->Data;
}

FString StaticCacheGeometryRenderer::GetGeometryLabel(GeometryType type, int textureHandle, bool repeat)
{
	FString label;
	label.Format("Geometry (type %d, handle %d, repeat %d)", (int)type, textureHandle, repeat ? 1 : 0);
	return label;
}

std::unique_ptr<GeometryData> StaticCacheGeometryRenderer::CreateGeometryData(GeometryType type, int textureHandle, bool repeat, int vboSize,
	GLLegacyTexture* overrideTexture, const FString* label)
{
	vboSize = std::max(vboSize, 32);
	FString useLabel = label != nullptr ? *label : GetGeometryLabel(type, textureHandle, repeat);

	GLLegacyTexture* texture;
	if (overrideTexture != nullptr)
		texture = overrideTexture->ParentArrayTexture != nullptr ? overrideTexture->ParentArrayTexture : overrideTexture;
	else
		texture = m_textureManager.GetParentArrayTexture(textureHandle, repeat);

	GLLegacyTexture* brightmapTexture = m_textureManager.GetBrightmapTexture(textureHandle, repeat);
	return std::make_unique<GeometryData>(textureHandle, texture, m_program, useLabel, vboSize, brightmapTexture);
}

GeometryData* StaticCacheGeometryRenderer::AllocateGeometryData(GeometryType type, int textureHandle, bool repeat, GLLegacyTexture* overrideTexture)
{
	std::unique_ptr<GeometryData> owned = CreateGeometryData(type, textureHandle, repeat, 0, overrideTexture, nullptr);
	GeometryData* data = owned.get();
	m_geometry.AddGeometry(type, std::move(owned));

	// Sorts textures that do not have transparent pixels first.
	// This is to get around the issue of middle textures with transparent pixels being drawn first and discarding stuff behind that should not be.
	if (type == GeometryType::TwoSidedMiddleWall)
	{
		auto& list = m_geometry.GetGeometry(type);
		std::stable_sort(list.begin(), list.end(), [](const std::unique_ptr<GeometryData>& x, const std::unique_ptr<GeometryData>& y)
			{ return TransparentGeometryCompare(*x, *y) < 0; });
	}

	m_textureToGeometryLookup.Add(type, textureHandle, repeat, data);
	return data;
}

void StaticCacheGeometryRenderer::ClearData(FLevelLocals* world, bool sameAsPreviousMap)
{
	Unsubscribe();
	m_world = nullptr;
	m_signaturesValid = false;

	if (sameAsPreviousMap)
	{
		m_geometry.ClearVbo();
		if (m_coverWallGeometry != nullptr) ClearVbo(m_coverWallGeometry->Vbo.get());
		if (m_coverWallGeometryOneSided != nullptr) ClearVbo(m_coverWallGeometryOneSided->Vbo.get());
		if (m_coverFlatGeometry != nullptr) ClearVbo(m_coverFlatGeometry->Vbo.get());
	}
	else
	{
		m_geometry.DisposeAndClear();
		m_textureToGeometryLookup.Clear();
		// the unique_ptrs dispose their vertex pipelines
		m_coverWallGeometry.reset();
		m_coverWallGeometryOneSided.reset();
		m_coverFlatGeometry.reset();
	}

	m_coverWallLookup.clear();
	m_coverFlatLookup.clear();

	m_freeManager.Clear();
	m_skyRenderer->Clear();
	SkyGeometryManager::Clear();

	m_transferHeightsLookup.clear();
	m_taggedSectors3D.clear();
}

void StaticCacheGeometryRenderer::ClearVbo(Buffer::Array::Vertex::VertexBufferObject<StaticVertex>* vbo)
{
	if (vbo == nullptr)
		return;

	memset(vbo->Data.Data(), 0, (size_t)vbo->Data.Max() * sizeof(StaticVertex));
	vbo->Data.Clear();
}

void StaticCacheGeometryRenderer::AddSectorPlane(sector_t* sectorForSubsectors, SectorPlaneFace face, bool floor, bool update, const RenderSector* renderSector,
	const RenderSector* lightLevelSector, const RenderPlane* geometryPlane, bool allowAlpha, Sector3D* sector3D, const RenderPlane* renderPlaneOverride)
{
	// Helion checks the plane of the sector selected by `floor`, not by `face` (3D floors render a ceiling plane as a floor).
	if (RenderPlane::From(sectorForSubsectors, floor ? SectorPlaneFace::Floor : SectorPlaneFace::Ceiling).NoRender)
		return;
	// UZDoom: portal planes are always dynamic (SD_Portal, plan M5.2).
	if (sector3D == nullptr && (sectorForSubsectors->renderDynamic[floor ? sector_t::floor : sector_t::ceiling] & SD_Portal) != 0)
		return;

	RenderPlane realPlane = RenderPlane::From(sectorForSubsectors, face);

	Data::RenderDataStyle style = Data::RenderDataStyle::Normal;
	float alpha = 1;
	if (sector3D != nullptr && (sector3D->Alpha < 1 || sector3D->RenderDataStyle != Data::RenderDataStyle::Normal))
	{
		style = sector3D->RenderDataStyle;
		if (style == Data::RenderDataStyle::Normal)
			style = Data::RenderDataStyle::Translucent;
		alpha = sector3D->Alpha;
	}

	RenderSector ownRenderSector;
	if (renderSector == nullptr)
	{
		ownRenderSector = GetRenderSector(sectorForSubsectors, TransferHeightView::Middle);
		renderSector = &ownRenderSector;
	}
	if (lightLevelSector == nullptr)
		lightLevelSector = renderSector;

	const RenderPlane& renderPlane = renderPlaneOverride != nullptr ? *renderPlaneOverride : renderSector->GetSectorPlane(face);
	int textureHandle = m_geometryRenderer.GetFlatTextureHandle(renderPlane.TextureHandle, allowAlpha);
	// Need to set to actual plane, not potential transfer heights plane.
	const RenderPlane& plane = realPlane;
	if (geometryPlane == nullptr)
		geometryPlane = &plane;

	RenderSector realSector(sectorForSubsectors);
	DynamicVertexArray* renderedVertices = nullptr;
	SkyVertexArray* renderedSkyVertices = nullptr;
	m_geometryRenderer.RenderSectorFlats(realSector, renderPlane, *geometryPlane, floor, false, renderedVertices, renderedSkyVertices,
		lightLevelSector, allowAlpha, alpha, style);

	AddSkyGeometry(nullptr, WallLocation::None, geometryPlane, renderedSkyVertices, realSector, update);

	if (renderedVertices == nullptr)
		return;

	if (sectorForSubsectors->GetHeightSec() != nullptr && m_coverFlatGeometry != nullptr &&
		(m_vanillaRender || (!m_vanillaRender && RenderStaticState::Sector(sectorForSubsectors).Flood)))
	{
		DynamicVertexArray* coverFlatVertices = nullptr;
		SkyVertexArray* coverSky = nullptr;
		m_geometryRenderer.RenderSectorFlats(realSector, renderPlane, *geometryPlane, floor, true, coverFlatVertices, coverSky);
		if (coverFlatVertices != nullptr)
			AddOrUpdateCoverFlatGeometry(sectorForSubsectors, plane, *coverFlatVertices);
	}

	StaticGeometryData& planeStatic = PlaneStatic(*geometryPlane);
	if (update)
	{
		UpdateVertices(planeStatic, textureHandle, SpanOf(renderedVertices), geometryPlane, nullptr, WallLocation::None, true);
		return;
	}

	GeometryType geometryType = ToGeometryType(style);
	TArray<StaticVertex>& vertices = GetTextureVertices(geometryType, textureHandle, true);
	GeometryData* geometryData = nullptr;
	if (m_textureToGeometryLookup.TryGetValue(geometryType, textureHandle, true, geometryData))
	{
		planeStatic.GeometryData = geometryData;
		planeStatic.Index = (int)vertices.Size();
		planeStatic.Length = (int)renderedVertices->size();
	}

	AddVertices(vertices, SpanOf(renderedVertices));
}

void StaticCacheGeometryRenderer::RenderWalls()
{
	RenderGeometryTypes(m_geometry.GetGeometry(GeometryType::Wall));
}

void StaticCacheGeometryRenderer::RenderTwoSidedMiddleWalls()
{
	RenderGeometryTypes(m_geometry.GetGeometry(GeometryType::TwoSidedMiddleWall));
}

void StaticCacheGeometryRenderer::RenderMiddle3D()
{
	RenderGeometryTypes(m_geometry.GetGeometry(GeometryType::Middle3D));
}

void StaticCacheGeometryRenderer::RenderFlats()
{
	RenderGeometryTypes(m_geometry.GetGeometry(GeometryType::Flat));
}

void StaticCacheGeometryRenderer::Render(GeometryType type)
{
	RenderGeometryTypes(m_geometry.GetGeometry(type));
}

void StaticCacheGeometryRenderer::RenderCoverWalls()
{
	RenderCoverInternal(m_coverWallGeometry.get());
}

void StaticCacheGeometryRenderer::RenderOneSidedCoverWalls()
{
	RenderCoverInternal(m_coverWallGeometryOneSided.get());
	RenderCoverInternal(m_coverFlatGeometry.get());
}

void StaticCacheGeometryRenderer::RenderCoverFlats()
{
	RenderCoverInternal(m_coverFlatGeometry.get());
}

void StaticCacheGeometryRenderer::RenderCoverInternal(GeometryData* data)
{
	if (data == nullptr)
		return;

	GLLegacyTexture* texture = data->Texture;
	texture->Bind(BindTextures::BoundTexture);
	GLLegacyTexture::Unbind(BindTextures::BrightmapTexture);

	data->Vbo->UploadCapacity();
	data->Pipeline->Bind(true);
	data->Pipeline->DrawArrays();
}

void StaticCacheGeometryRenderer::RenderGeometryTypes(RenderGeometry::GeometryList& geometry)
{
	if (geometry.size() == 0)
		return;

	for (size_t i = 0; i < geometry.size(); i++)
	{
		GeometryData& data = *geometry[i];
		if (data.Vbo->Count() == 0)
			continue;

		bool isNullCompatTex = data.TextureHandle <= 0;
		bool repeatY = (data.Texture->Flags & Texture::TextureFlags::ClampY) == Texture::TextureFlags::Default;
		// Special case for one-sided walls with no texture. Uses black texture to block rendering so use directly.
		GLLegacyTexture* texture = isNullCompatTex ? data.Texture : m_textureManager.GetTexture(data.TextureHandle, repeatY);
		if (Data::RenderWorldData::MaterialDraw && Data::RenderWorldData::MaterialDraw(texture, [&data]()
		{
			GLLegacyTexture::Unbind(BindTextures::BrightmapTexture);
			data.Vbo->UploadIfNeeded();
			data.Pipeline->Bind(true);
			data.Pipeline->DrawArrays();
		}))
			continue;

		texture->Bind(BindTextures::BoundTexture);

		GLLegacyTexture* brightmapTexture = isNullCompatTex ? nullptr : m_textureManager.GetBrightmapTexture(data.TextureHandle, repeatY);
		if (brightmapTexture != nullptr)
			brightmapTexture->Bind(BindTextures::BrightmapTexture);
		else
			GLLegacyTexture::Unbind(BindTextures::BrightmapTexture);

		data.Vbo->UploadIfNeeded();

		data.Pipeline->Bind(true);
		data.Pipeline->DrawArrays();
	}
}

void StaticCacheGeometryRenderer::RenderSkies(Shared::RenderInfo& renderInfo)
{
	m_skyRenderer->Render(renderInfo);
}

void StaticCacheGeometryRenderer::Dispose()
{
	if (m_disposed)
		return;

	Unsubscribe();
	m_geometry.DisposeAndClear();
	m_coverWallGeometry.reset();
	m_coverWallGeometryOneSided.reset();
	m_coverFlatGeometry.reset();
	m_disposed = true;
}

// --- movement events (World_SectorMove* of Helion)

void StaticCacheGeometryRenderer::World_SectorMove(sector_t* sector)
{
	auto it = m_taggedSectors3D.find(sector->Index());
	if (it == m_taggedSectors3D.end())
		return;

	for (Sector3D* sector3D : it->second)
	{
		sector_t* parent = sector3D->ParentSector;
		RenderStaticState::SetSectorMoveChanged(parent, sector_t::floor, GameTick());
		RenderStaticState::SetSectorMoveChanged(parent, sector_t::ceiling, GameTick());
		m_geometryRenderer.Sector3DTable().SetHeights3D(parent, ::Helion::World::Geometry::Sectors::SetHeightsMode::Update);

		for (auto& parentSector3D : m_geometryRenderer.Sector3DTable().Sectors3D(parent))
		{
			RenderStaticState::SetSectorMoveChanged(parentSector3D->FakeTop.Id, GameTick());
			RenderStaticState::SetSectorMoveChanged(parentSector3D->FakeBottom.Id, GameTick());
		}
	}
}

void StaticCacheGeometryRenderer::World_SectorMoveStart(sector_t* sector, int pos)
{
	auto it = m_transferHeightsLookup.find(sector->Index());
	if (it != m_transferHeightsLookup.end())
	{
		for (sector_t* transferSector : it->second)
		{
			HandleSectorMoveStart(transferSector, pos);
			RenderStaticState::SetSectorMoveChanged(transferSector, sector_t::floor, GameTick());
			RenderStaticState::SetSectorMoveChanged(transferSector, sector_t::ceiling, GameTick());
		}
	}

	WorldStatic::CheckCounter++;
	HandleSectorMoveStart(sector, pos);
}

bool StaticCacheGeometryRenderer::HandleSectorMoveStart(sector_t* sector, int pos, bool check3D, bool checkMovement, bool handlePlane)
{
	if (checkMovement && (sector->renderDynamic[pos] & SD_Movement) != 0)
		return false;

	RenderStaticState::Sector(sector).MoveEventGameTick = GameTick();
	StaticDataApplier::SetSectorDynamic(sector, pos == sector_t::floor ? ::Helion::World::Static::SectorPlanes_Floor : ::Helion::World::Static::SectorPlanes_Ceiling, SD_Movement);

	auto& planeState = RenderStaticState::Plane(sector, pos);
	if (handlePlane)
		ClearGeometryVertices(planeState.Static);

	if (m_vanillaRender)
	{
		auto cover = m_coverFlatLookup.find(CoverKey::MakeFlatKey(sector->Index(), pos == sector_t::floor ? SectorPlaneFace::Floor : SectorPlaneFace::Ceiling));
		if (cover != m_coverFlatLookup.end())
			ClearGeometryVertices(cover->second);
	}

	SkyGeometryManager::ClearGeometryVertices(sector->Index() * 2 + pos);
	HandleSectorMoveStartForLines(sector, !check3D);

	if (check3D)
	{
		int flipped = FlipPos(pos);
		auto tagged = m_taggedSectors3D.find(sector->Index());
		if (tagged != m_taggedSectors3D.end())
		{
			for (Sector3D* sector3D : tagged->second)
			{
				bool startSuccess = HandleSector3DPlaneMoveStart(sector3D, FakePlane(sector3D, pos, false), true);

				HandleSectorMoveStart(sector3D->ParentSector, flipped, false, true, false);

				// This can also affect rendering of 3D sectors in this parent sector.
				for (auto& parentSector3D : m_geometryRenderer.Sector3DTable().Sectors3D(sector3D->ParentSector))
				{
					if (parentSector3D.get() == sector3D)
						continue;

					HandleSectorMoveStart3D(parentSector3D.get());
				}

				// Need to ignore the movement check since it's flagged off the same plane.
				if (HasFakePlane(sector3D, pos, true))
					HandleSector3DPlaneMoveStart(sector3D, FakePlane(sector3D, pos, true), startSuccess);
			}
		}
	}

	return true;
}

bool StaticCacheGeometryRenderer::HandleSector3DPlaneMoveStart(Sector3D* sector3D, const RenderPlane& fakePlane, bool checkMovement)
{
	auto& planeState = RenderStaticState::Plane(fakePlane.Id);
	if (checkMovement && (planeState.Dynamic & SD_Movement) != 0)
		return false;

	planeState.Dynamic |= SD_Movement;
	ClearGeometryVertices(planeState.Static);
	SkyGeometryManager::ClearGeometryVertices(fakePlane.Id);
	HandleSectorMoveStartForLines3D(sector3D);
	return true;
}

const StaticCacheGeometryRenderer::RenderPlane& StaticCacheGeometryRenderer::FakePlane(const Sector3D* sector3D, int controlPos, bool flipped)
{
	if (controlPos == sector_t::floor)
		return flipped ? sector3D->FakeTopFlipped : sector3D->FakeTop;

	return flipped ? sector3D->FakeBottomFlipped : sector3D->FakeBottom;
}

bool StaticCacheGeometryRenderer::HasFakePlane(const Sector3D* sector3D, int controlPos, bool flipped)
{
	return !flipped || sector3D->HasFlipped;
}

bool StaticCacheGeometryRenderer::IsPlaneMoving(const sector_t* sector, int pos)
{
	return pos == sector_t::floor ? sector->floordata.ForceGet() != nullptr : sector->ceilingdata.ForceGet() != nullptr;
}

void StaticCacheGeometryRenderer::HandleSectorMoveStartForLines(sector_t* sector, bool checkOpposingSector3D)
{
	if (SectorLinesProcessed(RenderStaticState::Sector(sector), MoveState::Start))
		return;

	for (unsigned i = 0; i < sector->Lines.Size(); i++)
	{
		line_t* line = sector->Lines[i];
		side_t* front = line->sidedef[0];
		side_t* back = line->sidedef[1];
		UpdateSectorPlaneFloodFill(line);

		auto& frontState = RenderStaticState::Side(front);
		if (front->renderDynamic != 0 || frontState.UpperSky)
		{
			ClearSideGeometryVertices(front, WallLocation::Upper);
			SkyGeometryManager::ClearGeometryVertices(front, WallLocation::Upper);
		}

		if (front->renderDynamic != 0 || frontState.LowerSky)
		{
			ClearSideGeometryVertices(front, WallLocation::Lower);
			SkyGeometryManager::ClearGeometryVertices(front, WallLocation::Lower);
		}

		if (front->renderDynamic != 0)
		{
			ClearSideGeometryVertices(front, WallLocation::Middle);
			SkyGeometryManager::ClearGeometryVertices(front, WallLocation::Middle);

			if (frontState.HasFog)
				ClearGeometryVertices(frontState.Fog);
		}

		if (back == nullptr)
			continue;

		// Rendering against 3D sectors can be affected
		if (checkOpposingSector3D)
		{
			side_t* checkSide = front;
			if (front->sector == sector)
				checkSide = back;

			for (auto& sector3D : m_geometryRenderer.Sector3DTable().Sectors3D(checkSide->sector))
				HandleSectorMoveStart3D(sector3D.get());
		}

		auto& backState = RenderStaticState::Side(back);
		if (back->renderDynamic != 0 || backState.UpperSky)
		{
			ClearSideGeometryVertices(back, WallLocation::Upper);
			SkyGeometryManager::ClearGeometryVertices(back, WallLocation::Upper);
		}

		if (back->renderDynamic != 0 || backState.LowerSky)
		{
			ClearSideGeometryVertices(back, WallLocation::Lower);
			SkyGeometryManager::ClearGeometryVertices(back, WallLocation::Lower);
		}

		if (back->renderDynamic != 0)
		{
			ClearSideGeometryVertices(back, WallLocation::Middle);
			SkyGeometryManager::ClearGeometryVertices(back, WallLocation::Middle);

			if (backState.HasFog)
				ClearGeometryVertices(backState.Fog);
		}
	}
}

// The fake sector of a 3D floor has no lines of its own: its lines are the lines of the parent sector, and its walls
// are the Middle3D walls of the sides of those lines.
void StaticCacheGeometryRenderer::HandleSectorMoveStartForLines3D(Sector3D* sector3D)
{
	if (SectorLinesProcessed(RenderStaticState::SectorById(sector3D->SectorId), MoveState::Start))
		return;

	sector_t* parent = sector3D->ParentSector;
	for (unsigned i = 0; i < parent->Lines.Size(); i++)
	{
		line_t* line = parent->Lines[i];
		for (side_t* side : line->sidedef)
		{
			if (side == nullptr)
				continue;

			StaticGeometryData& wall = RenderStaticState::Wall3D(sector3D->SectorId, side, (int)WallLocation::Middle3D);
			ClearGeometryVertices(wall);
		}
	}
}

void StaticCacheGeometryRenderer::HandleSectorMoveStart3D(Sector3D* sector3D)
{
	if (sector3D->CheckCount == WorldStatic::CheckCounter)
		return;

	RenderStaticState::SetSectorMoveChanged(sector3D->FakeTop.Id, GameTick());
	RenderStaticState::SetSectorMoveChanged(sector3D->FakeBottom.Id, GameTick());
	HandleSector3DPlaneMoveStart(sector3D, sector3D->FakeTop, true);
	HandleSector3DPlaneMoveStart(sector3D, sector3D->FakeBottom, true);
}

void StaticCacheGeometryRenderer::ClearSideGeometryVertices(side_t* side, WallLocation location)
{
	ClearGeometryVertices(*MakeWall(side, location).Static);
	if (m_vanillaRender)
	{
		auto geometryData = m_coverWallLookup.find(CoverKey::MakeCoverWallKey(side->Index(), location, true));
		if (geometryData != m_coverWallLookup.end())
			ClearGeometryVertices(geometryData->second);
		geometryData = m_coverWallLookup.find(CoverKey::MakeCoverWallKey(side->Index(), location, false));
		if (geometryData != m_coverWallLookup.end())
			ClearGeometryVertices(geometryData->second);
	}
}

void StaticCacheGeometryRenderer::World_SectorMoveComplete(sector_t* sector, int pos)
{
	auto it = m_transferHeightsLookup.find(sector->Index());
	if (it != m_transferHeightsLookup.end())
	{
		for (sector_t* transferSector : it->second)
		{
			// Ignore if sector controlled by this moving transfer heights sector is still moving.
			// Movement clearing functions need to be handled when that move is complete.
			if (IsPlaneMoving(transferSector, pos))
				continue;

			HandleSectorMoveComplete(transferSector, pos);
		}
	}

	// Control sector is still moving. That sector needs to finalize the movement for this sector.
	sector_t* control = sector->GetHeightSec();
	if (control != nullptr && IsPlaneMoving(control, pos))
		return;

	WorldStatic::CheckCounter++;
	HandleSectorMoveComplete(sector, pos);
}

void StaticCacheGeometryRenderer::HandleSectorMoveComplete(sector_t* sector, int pos, bool check3D, bool handlePlane)
{
	RenderStaticState::Sector(sector).MoveEventGameTick = GameTick();
	StaticDataApplier::ClearSectorDynamicMovement(sector, pos);
	RefreshPlaneSignature(sector, pos);

	bool floor = pos == sector_t::floor;
	SectorPlaneFace face = floor ? SectorPlaneFace::Floor : SectorPlaneFace::Ceiling;
	m_geometryRenderer.SetBuffer(false);
	m_geometryRenderer.SetRenderMode(GeometryRenderMode::Dynamic, TransferHeightView::Middle);

	RenderPlane plane = RenderPlane::From(sector, face);
	if (floor)
		m_geometryRenderer.SetRenderFloor(plane);
	else
		m_geometryRenderer.SetRenderCeiling(plane);

	if (handlePlane)
		AddSectorPlane(sector, face, floor, true);

	HandleSectorMoveCompleteForLines(sector, !check3D);

	if (check3D)
	{
		int flipped = FlipPos(pos);
		SectorPlanes planes = floor ? SectorPlanes::Floor : SectorPlanes::Ceiling;
		auto tagged = m_taggedSectors3D.find(sector->Index());
		if (tagged != m_taggedSectors3D.end())
		{
			for (Sector3D* sector3D : tagged->second)
			{
				// Multiple 3D sectors can be moving. If any other is moving then ignore.
				if (HasOtherMovementSector3D(sector3D))
					continue;

				RenderStaticState::Plane(FakePlane(sector3D, pos, false).Id).Dynamic &= ~SD_Movement;

				if (handlePlane)
					HandleSectorMoveComplete(sector3D->ParentSector, flipped, false, false);

				for (auto& parentSector3D : m_geometryRenderer.Sector3DTable().Sectors3D(sector3D->ParentSector))
				{
					if (parentSector3D.get() == sector3D)
						continue;

					HandleSectorMoveComplete3D(parentSector3D.get());
				}

				if (HasFakePlane(sector3D, pos, true))
					RenderStaticState::Plane(FakePlane(sector3D, pos, true).Id).Dynamic &= ~SD_Movement;

				m_geometryRenderer.Sector3DTable().SetHeights3D(sector3D->ParentSector, ::Helion::World::Geometry::Sectors::SetHeightsMode::Update);
				AddSector3D(sector3D, planes, true);
			}
		}
	}
}

bool StaticCacheGeometryRenderer::HasOtherMovementSector3D(Sector3D* sector3D)
{
	// Reads the Sector3DTable through the control sectors; the table is the one of the owning GeometryRenderer, but the
	// moving test only needs the control sectors of the sector3D list that was captured in m_taggedSectors3D.
	sector_t* parent = sector3D->ParentSector;
	for (F3DFloor* ffloor : parent->e->XFloor.ffloors)
	{
		if (ffloor->model != nullptr && StaticDataApplier::IsMoving(ffloor->model))
			return true;
	}

	return false;
}

void StaticCacheGeometryRenderer::HandleSectorMoveComplete3D(Sector3D* sector3D)
{
	RenderStaticState::SetSectorMoveChanged(sector3D->FakeTop.Id, GameTick());
	RenderStaticState::SetSectorMoveChanged(sector3D->FakeBottom.Id, GameTick());
	RenderStaticState::Plane(sector3D->FakeTop.Id).Dynamic &= ~SD_Movement;
	RenderStaticState::Plane(sector3D->FakeBottom.Id).Dynamic &= ~SD_Movement;
	AddSector3D(sector3D, SectorPlanes::Floor | SectorPlanes::Ceiling, true);
}

void StaticCacheGeometryRenderer::HandleSectorMoveCompleteForLines(sector_t* sector, bool checkOpposingSector3D)
{
	// The other plane is still moving (both planes had movers, or a floor and a ceiling mover finish on the same tick):
	// the walls cannot be cached yet. Do not mark the lines processed, the last completing plane rebuilds them.
	if (StaticDataApplier::IsMoving(sector))
		return;

	if (SectorLinesProcessed(RenderStaticState::Sector(sector), MoveState::Complete))
		return;

	for (unsigned i = 0; i < sector->Lines.Size(); i++)
	{
		line_t* line = sector->Lines[i];
		AddLine(line, true);
		UpdateSectorPlaneFloodFill(line);

		if (line->sidedef[1] == nullptr)
			continue;

		side_t* front = line->sidedef[0];
		side_t* back = line->sidedef[1];
		if (checkOpposingSector3D)
		{
			side_t* checkSide = front;
			if (front->sector == sector)
				checkSide = back;

			for (auto& sector3D : m_geometryRenderer.Sector3DTable().Sectors3D(checkSide->sector))
				HandleSectorMoveComplete3D(sector3D.get());
		}

		CheckForFloodFill(front, back, GetRenderSector(front->sector, TransferHeightView::Middle),
			GetBackRenderSector(back->sector, TransferHeightView::Middle), true);
		CheckForFloodFill(back, front, GetRenderSector(back->sector, TransferHeightView::Middle),
			GetBackRenderSector(front->sector, TransferHeightView::Middle), true);
	}
}

bool StaticCacheGeometryRenderer::SectorLinesProcessed(RenderSectorState& state, MoveState moveState)
{
	if (state.MoveState == moveState && (state.CheckCount == WorldStatic::CheckCounter || state.MoveEventGameTick == state.MoveProcessedGameTick))
		return true;

	state.MoveState = moveState;
	state.CheckCount = WorldStatic::CheckCounter;
	state.MoveProcessedGameTick = RenderStaticState::GameTick();
	return false;
}

void StaticCacheGeometryRenderer::World_SideTextureChanged(side_t* side, int tier, FTextureID textureId, FTextureID previousTextureId)
{
	WallLocation location = LocationOf(tier);
	auto& state = RenderStaticState::Side(side);

	// Previously the texture was not part of the vertex. Now it's the array index which means the vertex needs to be rebuilt.
	state.LastRenderChangeGametick = GameTick();
	RefreshSideSignature(side, tier);
	ClearSideGeometryVertices(side, location);

	StaticGeometryData& wallStatic = state.Static[tier];
	if (wallStatic.GeometryData != nullptr)
		m_freeManager.Add(wallStatic, m_geometryRenderer.GetWallType(side, location, nullptr), GetRepeatY(side, location));
	wallStatic.GeometryData = nullptr;

	side_t* partner = side->linedef->sidedef[1] == nullptr ? nullptr : PartnerSide(side);
	if (partner != nullptr && (location == WallLocation::Upper || location == WallLocation::Lower) &&
		(textureId.GetIndex() <= 0 || previousTextureId.GetIndex() <= 0))
	{
		CheckForFloodFill(side, partner, side->sector, partner->sector, IsFrontSide(side));
	}

	m_geometryRenderer.SetBuffer(false);
	m_geometryRenderer.SetRenderMode(GeometryRenderMode::Dynamic, TransferHeightView::Middle);
	AddLine(side->linedef, true);

	// 3D floors controlled by the sector of the side, and the 3D floors of the sector on the other side.
	auto tagged = m_taggedSectors3D.find(side->sector->Index());
	if (tagged != m_taggedSectors3D.end())
	{
		for (Sector3D* sector3D : tagged->second)
		{
			if (sector3D->GetLogicalWallLocation() == tier)
				AddSector3D(sector3D, SectorPlanes::None, true, true);
		}
	}

	if (partner != nullptr)
	{
		for (auto& sector3D : m_geometryRenderer.Sector3DTable().Sectors3D(partner->sector))
		{
			if (sector3D->GetLogicalWallLocation() == tier)
				AddSector3D(sector3D.get(), SectorPlanes::None, true, true);
		}
	}
}

void StaticCacheGeometryRenderer::World_PlaneTextureChanged(sector_t* sector, int pos, FTextureID textureId, FTextureID previousTextureId)
{
	bool floor = pos == sector_t::floor;
	SectorPlaneFace face = floor ? SectorPlaneFace::Floor : SectorPlaneFace::Ceiling;
	int planeId = sector->Index() * 2 + pos;
	auto& planeState = RenderStaticState::Plane(planeId);

	planeState.LastRenderChangeGametick = GameTick();
	RefreshPlaneSignature(sector, pos);
	SkyGeometryManager::ClearGeometryVertices(planeId);
	if (planeState.Static.GeometryData != nullptr && ClearGeometryVertices(planeState.Static))
		m_freeManager.Add(planeState.Static, GeometryType::Flat, true);

	planeState.Static.GeometryData = nullptr;

	m_geometryRenderer.SetBuffer(false);
	m_geometryRenderer.SetRenderMode(GeometryRenderMode::Dynamic, TransferHeightView::Middle);

	auto tagged = m_taggedSectors3D.find(sector->Index());
	if (tagged != m_taggedSectors3D.end())
	{
		SectorPlanes sectorPlanes = floor ? SectorPlanes::Floor : SectorPlanes::Ceiling;
		for (Sector3D* sector3D : tagged->second)
		{
			const RenderPlane& plane3D = FakePlane(sector3D, pos, false);
			StaticGeometryData& plane3DStatic = PlaneStatic(plane3D);
			if (plane3DStatic.GeometryData != nullptr && ClearGeometryVertices(plane3DStatic))
				m_freeManager.Add(plane3DStatic, GeometryType::Flat, true);

			plane3DStatic.GeometryData = nullptr;

			if (!IsPlaneMoving(sector3D->ControlSector, pos))
			{
				// Refreshes the plane snapshots of the 3D floor with the new texture of the control plane.
				m_geometryRenderer.Sector3DTable().SetHeights3D(sector3D->ParentSector, ::Helion::World::Geometry::Sectors::SetHeightsMode::Update);
				AddSectorPlanes3D(sector3D, sectorPlanes, true);
			}
		}
	}

	AddSectorPlane(sector, face, floor, true);
}

bool StaticCacheGeometryRenderer::ClearGeometryVertices(const StaticGeometryData& data)
{
	if (data.GeometryData == nullptr)
		return false;

	ClearGeometryVerticesRaw(*data.GeometryData, data.Index, data.Length);
	return true;
}

void StaticCacheGeometryRenderer::ClearGeometryVerticesRaw(GeometryData& geometryData, int startIndex, int length)
{
	memset(geometryData.Vbo->Data.Data() + startIndex, 0, sizeof(StaticVertex) * (size_t)length);
	geometryData.Vbo->UploadSubData(startIndex, length);
}

void StaticCacheGeometryRenderer::UpdateVertices(StaticGeometryData& staticGeometry, int textureHandle, DynamicVertexSpan vertices, const RenderPlane* plane,
	side_t* side, WallLocation wallLocation, bool repeat, GLLegacyTexture* texture, Sector3D* sector3D, bool isOneSidedLine)
{
	bool isWall = side != nullptr && wallLocation != WallLocation::None;
	GeometryType geometryType = isWall ? m_geometryRenderer.GetWallType(side, wallLocation, sector3D) : GeometryType::Flat;
	if (isWall && NeedsCoverWall(geometryType))
		AddOrUpdateCoverWall(side, vertices, wallLocation, wallLocation == WallLocation::Middle);

	if (textureHandle <= 0 && !isOneSidedLine)
		return;

	// This doesn't practically matter but makes debugging nicer
	if (texture != nullptr)
		texture = texture->ParentArrayTexture != nullptr ? texture->ParentArrayTexture : texture;

	// If this surface generated more vertices than previously cached, release so a new one can be requested. (happens with 3D sectors)
	if (staticGeometry.GeometryData != nullptr && staticGeometry.Length < (int)vertices.size())
	{
		if (plane != nullptr)
			m_freeManager.Add(staticGeometry, GeometryType::Flat, true);
		else if (isWall)
			m_freeManager.Add(staticGeometry, m_geometryRenderer.GetWallType(side, wallLocation, sector3D), GetRepeatY(side, wallLocation));
		staticGeometry.GeometryData = nullptr;
	}

	if (staticGeometry.GeometryData == nullptr)
	{
		AddNewGeometry(textureHandle, vertices, geometryType, staticGeometry, plane, side, wallLocation, repeat, texture, sector3D);
		return;
	}

	int startIndex = staticGeometry.Index;
	auto& vbo = *staticGeometry.GeometryData->Vbo;
	CopyVertices(vbo.Data.Data(), vertices, startIndex);

	if (m_worldReload)
		vbo.SetNotUploaded();
	else
		vbo.UploadSubData(startIndex, (int)vertices.size());

	// On map reloads the Vbo length is cleared. This ensures it's expanded back out correctly.
	int length = std::max((int)vbo.Data.Size(), startIndex + (int)vertices.size());
	if ((int)vbo.Data.Size() < length)
		vbo.Data.Resize((unsigned)length);
}

void StaticCacheGeometryRenderer::AddOrUpdateCoverWall(side_t* side, DynamicVertexSpan sideVertices, WallLocation location, bool oneSided)
{
	if (m_coverWallGeometry == nullptr || m_coverWallGeometryOneSided == nullptr)
		return;

	assert(sideVertices.size() % 6 == 0 && "Incorrect number of vertices. Must be sets of 6 per wall.");

	if (sideVertices.size() > 6)
	{
		// This is for 3D sectors. Walls are sliced against other 3D sectors from top to bottom.
		// OneSided middle walls can always take the first 6 since top and bottom Z values are projected.
		if (oneSided && location == WallLocation::Middle)
		{
			sideVertices = sideVertices.first(6);
		}
		else
		{
			// Upper: Take the last 6 vertices since this is the lowest Z portion. CopyCoverWallVertices projects their top heights.
			// Lower: the first 6 since it's the highest Z portion. CopyCoverWallVertices projects their bottom heights.
			switch (location)
			{
			case WallLocation::Middle:
			case WallLocation::Middle3D:
				sideVertices = CreateMiddleCoverWall3D(sideVertices);
				break;
			case WallLocation::Upper:
				sideVertices = sideVertices.last(6);
				break;
			default:
				sideVertices = sideVertices.first(6);
				break;
			}
		}
	}

	// This is uploaded as the max possible value so UploadSubData can be used even if it's new.
	CoverKey key = CoverKey::MakeCoverWallKey(side->Index(), location, oneSided);
	int length = (int)sideVertices.size();
	auto found = m_coverWallLookup.find(key);
	if (found != m_coverWallLookup.end() && found->second.GeometryData != nullptr)
	{
		StaticGeometryData& staticGeometryData = found->second;
		auto& geometryVbo = *staticGeometryData.GeometryData->Vbo;
		EnsureCoverWallVboCapacity(staticGeometryData, sideVertices, geometryVbo);

		CoverWallUtil::CopyCoverWallVertices(side, geometryVbo.Data.Data(), sideVertices, staticGeometryData.Index, location);
		HandleCoverWallUpload(geometryVbo, staticGeometryData.Index, length);
		int newSize = std::max((int)geometryVbo.Data.Size(), staticGeometryData.Index + length);
		if ((int)geometryVbo.Data.Size() < newSize)
			geometryVbo.Data.Resize((unsigned)newSize);
	}
	else
	{
		GeometryData* useGeometry = oneSided ? m_coverWallGeometryOneSided.get() : m_coverWallGeometry.get();
		auto& geometryVbo = *useGeometry->Vbo;
		StaticGeometryData staticGeometryData(useGeometry, (int)geometryVbo.Data.Size(), length);
		EnsureCoverWallVboCapacity(staticGeometryData, sideVertices, geometryVbo);
		CoverWallUtil::CopyCoverWallVertices(side, geometryVbo.Data.Data(), sideVertices, staticGeometryData.Index, location);
		geometryVbo.Data.Resize((unsigned)((int)geometryVbo.Data.Size() + length));
		m_coverWallLookup[key] = staticGeometryData;

		HandleCoverWallUpload(geometryVbo, staticGeometryData.Index, length);
	}
}

StaticCacheGeometryRenderer::DynamicVertexSpan StaticCacheGeometryRenderer::CreateMiddleCoverWall3D(DynamicVertexSpan sideVertices)
{
	float lowestZ = sideVertices[sideVertices.size() - 1].Z;
	for (int i = 0; i < 6; i++)
		m_coverWallVertices3D[i] = sideVertices[(size_t)i];

	m_coverWallVertices3D[1].Z = lowestZ;
	m_coverWallVertices3D[3].Z = lowestZ;
	m_coverWallVertices3D[5].Z = lowestZ;
	return DynamicVertexSpan(m_coverWallVertices3D, 6);
}

void StaticCacheGeometryRenderer::HandleCoverWallUpload(Buffer::Array::Vertex::VertexBufferObject<StaticVertex>& geometryVbo, int index, int length)
{
	if (m_worldReload)
		geometryVbo.SetNotUploaded();
	else
		geometryVbo.UploadSubDataOrSetNotUploaded(index, length);
}

void StaticCacheGeometryRenderer::EnsureCoverWallVboCapacity(const StaticGeometryData& staticGeometryData, DynamicVertexSpan sideVertices,
	Buffer::Array::Vertex::VertexBufferObject<StaticVertex>& geometryVbo)
{
	// This generally shouldn't happen but vanilla sprite clipping emulation renders two-sided middle cover walls that may overflow the original calculation since textures can move/change.
	int required = staticGeometryData.Index + (int)sideVertices.size();
	if ((int)geometryVbo.Data.Max() < required)
	{
		geometryVbo.Data.Grow((unsigned)(required - (int)geometryVbo.Data.Size()));
		geometryVbo.SetNotUploaded();
	}
}

void StaticCacheGeometryRenderer::AddOrUpdateCoverFlatGeometry(sector_t* sector, const RenderPlane& plane, const DynamicVertexArray& vertices)
{
	if (!m_vanillaRender || m_coverFlatGeometry == nullptr)
		return;

	RenderSector renderSector = GetRenderSector(sector, TransferHeightView::Middle);
	// Don't need this cover flat if they are equal.
	if (renderSector.GetSectorPlane(plane.Facing).Z == plane.Z)
		return;

	CoverKey key = CoverKey::MakeFlatKey(sector->Index(), plane.Facing);
	auto& vbo = *m_coverFlatGeometry->Vbo;
	DynamicVertexSpan span(const_cast<DynamicVertex*>(vertices.data()), vertices.size());
	auto found = m_coverFlatLookup.find(key);
	if (found != m_coverFlatLookup.end())
	{
		StaticGeometryData& coverGeometry = found->second;
		int newLength = coverGeometry.Index + coverGeometry.Length;
		if ((int)vbo.Data.Max() < newLength)
		{
			vbo.Data.Grow((unsigned)(newLength - (int)vbo.Data.Size()));
			CopyVertices(vbo.Data.Data(), span, coverGeometry.Index);
			vbo.SetNotUploaded();
		}
		else
		{
			CopyVertices(vbo.Data.Data(), span, coverGeometry.Index);
			vbo.UploadSubData(coverGeometry.Index, coverGeometry.Length);
		}
	}
	else
	{
		m_coverFlatLookup[key] = StaticGeometryData(m_coverFlatGeometry.get(), (int)vbo.Data.Size(), (int)vertices.size());
		AddVertices(vbo.Data, span);
	}
}

void StaticCacheGeometryRenderer::AddNewGeometry(int textureHandle, DynamicVertexSpan vertices, GeometryType geometryType, StaticGeometryData& staticGeometry,
	const RenderPlane* plane, side_t* side, WallLocation wallLocation, bool repeatY, GLLegacyTexture* texture, Sector3D* sector3D)
{
	StaticGeometryData existing;
	if (m_freeManager.GetAndRemove(textureHandle, geometryType, repeatY, (int)vertices.size(), existing))
	{
		staticGeometry = existing;
		UpdateVertices(staticGeometry, textureHandle, vertices, plane, side, wallLocation, repeatY, texture, sector3D);
		return;
	}

	// This texture exists, append to the vbo
	GeometryData* data = nullptr;
	if (m_textureToGeometryLookup.TryGetValue(geometryType, textureHandle, repeatY, data))
	{
		SetRuntimeGeometryData(staticGeometry, side, wallLocation, textureHandle, data, vertices, repeatY, sector3D);
		AddVertices(data->Vbo->Data, vertices);
		// TODO this causes the entire vbo to be uploaded when we could use sub-buffer
		data->Vbo->SetNotUploaded();
		return;
	}

	data = AllocateGeometryData(geometryType, textureHandle, repeatY, texture);
	SetRuntimeGeometryData(staticGeometry, side, wallLocation, textureHandle, data, vertices, repeatY, sector3D);
	AddVertices(data->Vbo->Data, vertices);
	data->Vbo->SetNotUploaded();
}

void StaticCacheGeometryRenderer::SetRuntimeGeometryData(StaticGeometryData& staticGeometry, side_t* side, WallLocation wallLocation, int textureHandle,
	GeometryData* geometryData, DynamicVertexSpan vertices, bool repeat, Sector3D* sector3D)
{
	if (side != nullptr && wallLocation != WallLocation::None)
	{
		SetSideData(staticGeometry, m_geometryRenderer.GetWallType(side, wallLocation, sector3D), textureHandle, geometryData->Vbo->Count(), (int)vertices.size(), repeat, geometryData);
		return;
	}

	staticGeometry.GeometryData = geometryData;
	staticGeometry.Index = geometryData->Vbo->Count();
	staticGeometry.Length = (int)vertices.size();
}

}
