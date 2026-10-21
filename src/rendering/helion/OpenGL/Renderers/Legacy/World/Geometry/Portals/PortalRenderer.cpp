/*
** PortalRenderer.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Portals.PortalRenderer.
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

#include "PortalRenderer.h"

#include <cassert>
#include <cfloat>
#include "g_levellocals.h"
#include "r_defs.h"
#include "r_sky.h"
#include "Common/Shared/World/WorldTriangulator.h"
#include "World/Static/RenderStaticState.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

using FloodFill::FloodFillRenderer;
using FloodFill::FloodFillRenderMode;
using ::Helion::World::Static::RenderStaticState;
using Shared::World::WallVertices;
using Shared::World::WorldTriangulator;

PortalRenderer::PortalRenderer(Texture::Legacy::LegacyGLTextureManager& glTextureManager)
	: m_floodFillStatic(glTextureManager, FloodFillRenderMode::Static), m_floodFillDynamic(glTextureManager, FloodFillRenderMode::Dynamic)
{
	m_floodFillRenderer = &m_floodFillStatic;
	m_transferHeightView = TransferHeightView::Middle;
}

void PortalRenderer::SetRenderMode(GeometryRenderMode renderMode, TransferHeightView view, bool clearVertices)
{
	m_transferHeightView = view;

	if (renderMode == GeometryRenderMode::Dynamic)
	{
		m_floodFillRenderer = &m_floodFillStatic;
		return;
	}

	m_floodFillRenderer = &m_floodFillDynamic;
	if (clearVertices)
		m_floodFillDynamic.ClearVertices();
}

void PortalRenderer::UpdateTo(FLevelLocals* world)
{
	m_floodFillStatic.UpdateTo(world);
	m_floodFillDynamic.UpdateTo(world);
}

void PortalRenderer::ClearStaticWall(int floodKey)
{
	m_floodFillRenderer->ClearStaticWall(floodKey);
}

FloodSet PortalRenderer::AddStaticFloodFillSide(side_t* facingSide, side_t* otherSide, const RenderSector& floodSector, SideTexture sideTexture, bool isFront,
	FloodFillRenderer* renderer)
{
	return HandleStaticFloodFillSide(facingSide, otherSide, floodSector, sideTexture, isFront, false, renderer);
}

FloodSet PortalRenderer::UpdateStaticFloodFillSide(side_t* facingSide, side_t* otherSide, const RenderSector& floodSector, SideTexture sideTexture, bool isFront,
	FloodFillRenderer* renderer)
{
	return HandleStaticFloodFillSide(facingSide, otherSide, floodSector, sideTexture, isFront, true, renderer);
}

void PortalRenderer::AddFloodFillPlane(side_t* facingSide, const RenderSector& floodSector, SectorPlanes planes, SectorPlaneFace face, bool isFront,
	FloodFillRenderer* renderer)
{
	HandleFloodFillPlane(facingSide, floodSector, planes, face, isFront, false, renderer);
}

void PortalRenderer::UpdateFloodFillPlane(side_t* facingSide, const RenderSector& floodSector, SectorPlanes planes, SectorPlaneFace face, bool isFront,
	FloodFillRenderer* renderer)
{
	HandleFloodFillPlane(facingSide, floodSector, planes, face, isFront, true, renderer);
}

void PortalRenderer::HandleFloodFillPlane(side_t* facingSide, const RenderSector& floodSector, SectorPlanes planes, SectorPlaneFace face, bool isFront,
	bool update, FloodFillRenderer* useRenderer)
{
	FloodFillRenderer* renderer = useRenderer != nullptr ? useRenderer : m_floodFillRenderer;
	WallVertices wall;
	auto& state = RenderStaticState::Side(facingSide);

	// The fake planes are flat offsets of the flooded plane: no sector, so no slope.
	m_fakeFloor.Sector = nullptr;
	m_fakeCeiling.Sector = nullptr;

	if (face == SectorPlaneFace::Floor)
	{
		const RenderPlane& top = floodSector.Floor;
		m_fakeFloor.TextureHandle = floodSector.Floor.TextureHandle;
		m_fakeFloor.Z = top.Z - FakeWallHeight;
		m_fakeFloor.PrevZ = floodSector.Floor.PrevZ - FakeWallHeight;
		m_fakeFloor.LightLevelAdd = floodSector.LightLevel;

		WorldTriangulator::HandleTwoSidedLower(facingSide, top, m_fakeFloor, ::Helion::Geometry::Vec2F(0, 0), isFront, wall);

		if (update || m_transferHeightView != TransferHeightView::Middle)
			renderer->UpdateStaticWall(state.FloorFloodKey, floodSector.Floor, wall, top.Z, DBL_MAX, SideTexture::None, -1, true);
		else
			state.FloorFloodKey = renderer->AddStaticWall(floodSector.Floor, wall, top.Z, DBL_MAX, SideTexture::None, -1, true);
	}
	else
	{
		const RenderPlane& bottom = floodSector.Ceiling;
		m_fakeCeiling.TextureHandle = floodSector.Ceiling.TextureHandle;
		m_fakeCeiling.Z = bottom.Z + FakeWallHeight;
		m_fakeCeiling.PrevZ = floodSector.Ceiling.PrevZ + FakeWallHeight;
		m_fakeCeiling.LightLevelAdd = floodSector.LightLevel;

		WorldTriangulator::HandleTwoSidedUpper(facingSide, m_fakeCeiling, bottom, ::Helion::Geometry::Vec2F(0, 0), isFront, wall);

		if (update || m_transferHeightView != TransferHeightView::Middle)
			renderer->UpdateStaticWall(state.CeilingFloodKey, floodSector.Ceiling, wall, -DBL_MAX, bottom.Z, SideTexture::None, -1, true);
		else
			state.CeilingFloodKey = renderer->AddStaticWall(floodSector.Ceiling, wall, -DBL_MAX, bottom.Z, SideTexture::None, -1, true);
	}
}

FloodSet PortalRenderer::HandleStaticFloodFillSide(side_t* facingSide, side_t* otherSide, const RenderSector& floodSector, SideTexture sideTexture, bool isFront,
	bool update, FloodFillRenderer* useRenderer)
{
	FloodSet result = FloodSet::None;
	FloodFillRenderer* renderer = useRenderer != nullptr ? useRenderer : m_floodFillRenderer;
	WallVertices wall;
	RenderSector facingSector = ::Helion::World::Geometry::Sectors::GetRenderSector(facingSide->sector, m_transferHeightView);
	RenderSector otherSector = ::Helion::World::Geometry::Sectors::GetBackRenderSector(otherSide->sector, m_transferHeightView);

	int lineId = facingSide->linedef->Index();
	auto& state = RenderStaticState::Side(facingSide);

	m_fakeFloor.Sector = nullptr;
	m_fakeCeiling.Sector = nullptr;

	if (sideTexture == SideTexture::Upper)
	{
		const RenderPlane* top = &facingSector.Ceiling;
		const RenderPlane* bottom = &otherSector.Ceiling;
		WorldTriangulator::HandleTwoSidedUpper(facingSide, *top, *bottom, ::Helion::Geometry::Vec2F(0, 0), isFront, wall);
		double floodMaxZ = bottom->Z;
		if (!IsSky(floodSector.Ceiling))
		{
			result |= FloodSet::Normal;
			if (update || m_transferHeightView != TransferHeightView::Middle)
				renderer->UpdateStaticWall(state.UpperFloodKeys.Key1, floodSector.Ceiling, wall, -DBL_MAX, floodMaxZ, sideTexture, lineId);
			else
				state.UpperFloodKeys.Key1 = renderer->AddStaticWall(floodSector.Ceiling, wall, -DBL_MAX, floodMaxZ, sideTexture, lineId);
		}

		if (IgnoreAltFloodFill(facingSide, otherSide, SectorPlaneFace::Ceiling))
			return result;

		result |= FloodSet::Alt;

		bottom = &facingSector.Ceiling;
		m_fakeCeiling.TextureHandle = floodSector.Ceiling.TextureHandle;
		m_fakeCeiling.Z = bottom->Z + FakeWallHeight;
		m_fakeCeiling.PrevZ = bottom->Z + FakeWallHeight;
		m_fakeCeiling.LightLevelAdd = floodSector.LightLevel;
		WorldTriangulator::HandleTwoSidedLower(facingSide, m_fakeCeiling, *bottom, ::Helion::Geometry::Vec2F(0, 0), !isFront, wall);

		double min = floodMaxZ;
		double max = DBL_MAX;

		if (update || m_transferHeightView != TransferHeightView::Middle)
			renderer->UpdateStaticWall(state.UpperFloodKeys.Key2, facingSector.Ceiling, wall, min, max, sideTexture, lineId);
		else
			state.UpperFloodKeys.Key2 = renderer->AddStaticWall(facingSector.Ceiling, wall, min, max, sideTexture, lineId);
	}
	else
	{
		assert(sideTexture == SideTexture::Lower && "Expected lower floor");
		const RenderPlane* top = &otherSector.Floor;
		const RenderPlane* bottom = &facingSector.Floor;
		double floodMinZ = top->Z;

		// This lower would clip into the upper texture. Pick the upper as the priority and stop at the ceiling.
		// TODO there is a case here where it should HOM.
		if (top->Z > otherSector.Ceiling.Z)
			top = &otherSector.Ceiling;

		WorldTriangulator::HandleTwoSidedLower(facingSide, *top, *bottom, ::Helion::Geometry::Vec2F(0, 0), isFront, wall);

		if (!IsSky(floodSector.Floor))
		{
			result |= FloodSet::Normal;
			if (update || m_transferHeightView != TransferHeightView::Middle)
				renderer->UpdateStaticWall(state.LowerFloodKeys.Key1, floodSector.Floor, wall, floodMinZ, DBL_MAX, sideTexture, lineId);
			else
				state.LowerFloodKeys.Key1 = renderer->AddStaticWall(floodSector.Floor, wall, floodMinZ, DBL_MAX, sideTexture, lineId);
		}

		if (IgnoreAltFloodFill(facingSide, otherSide, SectorPlaneFace::Floor))
			return result;

		// This is the alternate case where the floor will flood with the surrounding sector when the camera goes below the flood sector z.
		result |= FloodSet::Alt;
		top = &facingSector.Floor;
		m_fakeFloor.TextureHandle = floodSector.Floor.TextureHandle;
		m_fakeFloor.Z = bottom->Z - FakeWallHeight;
		m_fakeFloor.PrevZ = bottom->Z - FakeWallHeight;
		m_fakeFloor.LightLevelAdd = floodSector.LightLevel;
		WorldTriangulator::HandleTwoSidedLower(facingSide, *top, m_fakeFloor, ::Helion::Geometry::Vec2F(0, 0), !isFront, wall);

		double min = -DBL_MAX;
		double max = floodMinZ;

		if (update || m_transferHeightView != TransferHeightView::Middle)
			renderer->UpdateStaticWall(state.LowerFloodKeys.Key2, facingSector.Floor, wall, min, max, sideTexture, lineId);
		else
			state.LowerFloodKeys.Key2 = renderer->AddStaticWall(facingSector.Floor, wall, min, max, sideTexture, lineId);
	}

	return result;
}

bool PortalRenderer::IgnoreAltFloodFill(side_t* facingSide, side_t* otherSide, SectorPlaneFace face)
{
	return IsSky(facingSide->sector, face) || IsSky(otherSide->sector, face);
}

void PortalRenderer::Render(Shared::RenderInfo& renderInfo)
{
	m_floodFillRenderer->Render(renderInfo);
}

void PortalRenderer::RenderWallClip(Shared::RenderInfo& renderInfo)
{
	m_floodFillRenderer->RenderWallClip(renderInfo);
}

bool PortalRenderer::IsSky(const RenderPlane& plane)
{
	return plane.TextureHandle == skyflatnum.GetIndex();
}

bool PortalRenderer::IsSky(const sector_t* sector, SectorPlaneFace face)
{
	return sector->GetTexture((int)face) == skyflatnum;
}

}
