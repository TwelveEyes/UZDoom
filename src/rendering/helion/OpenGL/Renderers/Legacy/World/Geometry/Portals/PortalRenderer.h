/*
** PortalRenderer.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Portals.PortalRenderer (flood fill portals only; the
** UZDoom portal kinds are added in M5 next to this class).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Side -> side_t*, Sector -> RenderSector (implicitly built from a sector_t*, see World/Geometry/Sectors/RenderSector.h).
** The flood keys of a side (Side.FloorFloodKey, UpperFloodKeys ...) are RenderSideState members (World/Static/RenderStaticState.h).
** Line.RenderSegStart / RenderSegEnd do not exist (the line gap push of Helion is not ported), so they are not saved and restored.
**
*/

#pragma once

#include <memory>
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/FloodFill/FloodFillRenderer.h"
#include "FloodFillOptions.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderMode.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"
#include "World/Geometry/Sectors/RenderSector.h"
#include "World/Geometry/Sectors/SectorPlaneFace.h"
#include "World/Geometry/Sectors/TransferHeightsView.h"
#include "World/Static/SideTexture.h"

struct side_t;
struct FLevelLocals;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

// [Flags]
enum class FloodSet
{
	None = 0,
	Normal = 1,
	Alt = 2
};

constexpr FloodSet operator|(FloodSet a, FloodSet b) { return (FloodSet)((int)a | (int)b); }
constexpr FloodSet operator&(FloodSet a, FloodSet b) { return (FloodSet)((int)a & (int)b); }
constexpr FloodSet& operator|=(FloodSet& a, FloodSet b) { a = a | b; return a; }

class PortalRenderer
{
public:
	using RenderSector = ::Helion::World::Geometry::Sectors::RenderSector;
	using RenderPlane = ::Helion::World::Geometry::Sectors::RenderPlane;
	using SectorPlanes = ::Helion::World::Geometry::Sectors::SectorPlanes;
	using SectorPlaneFace = ::Helion::World::Geometry::Sectors::SectorPlaneFace;
	using TransferHeightView = ::Helion::World::Geometry::Sectors::TransferHeightView;
	using SideTexture = ::Helion::World::Static::SideTexture;

	explicit PortalRenderer(Texture::Legacy::LegacyGLTextureManager& glTextureManager);

	PortalRenderer(const PortalRenderer&) = delete;
	PortalRenderer& operator=(const PortalRenderer&) = delete;

	FloodFill::FloodFillRenderer& GetStaticFloodFillRenderer() { return m_floodFillStatic; }

	void SetRenderMode(GeometryRenderMode renderMode, TransferHeightView view, bool clearVertices);

	void UpdateTo(FLevelLocals* world);

	void ClearStaticWall(int floodKey);

	FloodSet AddStaticFloodFillSide(side_t* facingSide, side_t* otherSide, const RenderSector& floodSector, SideTexture sideTexture, bool isFront,
		FloodFill::FloodFillRenderer* renderer = nullptr);
	FloodSet UpdateStaticFloodFillSide(side_t* facingSide, side_t* otherSide, const RenderSector& floodSector, SideTexture sideTexture, bool isFront,
		FloodFill::FloodFillRenderer* renderer = nullptr);

	void AddFloodFillPlane(side_t* facingSide, const RenderSector& floodSector, SectorPlanes planes, SectorPlaneFace face, bool isFront,
		FloodFill::FloodFillRenderer* renderer = nullptr);
	void UpdateFloodFillPlane(side_t* facingSide, const RenderSector& floodSector, SectorPlanes planes, SectorPlaneFace face, bool isFront,
		FloodFill::FloodFillRenderer* renderer = nullptr);

	void Render(Shared::RenderInfo& renderInfo);
	void RenderWallClip(Shared::RenderInfo& renderInfo);

private:
	static constexpr int FakeWallHeight = 16384;	// Constants.MaxTextureHeight

	FloodFill::FloodFillRenderer* m_floodFillRenderer;
	FloodFill::FloodFillRenderer m_floodFillStatic;
	FloodFill::FloodFillRenderer m_floodFillDynamic;
	RenderPlane m_fakeFloor;
	RenderPlane m_fakeCeiling;
	TransferHeightView m_transferHeightView;

	void HandleFloodFillPlane(side_t* facingSide, const RenderSector& floodSector, SectorPlanes planes, SectorPlaneFace face, bool isFront, bool update,
		FloodFill::FloodFillRenderer* useRenderer);
	FloodSet HandleStaticFloodFillSide(side_t* facingSide, side_t* otherSide, const RenderSector& floodSector, SideTexture sideTexture, bool isFront, bool update,
		FloodFill::FloodFillRenderer* useRenderer);
	static bool IgnoreAltFloodFill(side_t* facingSide, side_t* otherSide, SectorPlaneFace face);
	static bool IsSky(const RenderPlane& plane);
	static bool IsSky(const sector_t* sector, SectorPlaneFace face);
};

}
