/*
** LegacyWorldRenderer.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.LegacyWorldRenderer.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** GL state calls map onto FRenderState (State() below): GL.Enable(PolygonOffsetFill)+PolygonOffset is
** SetPolygonOffset / ClearDepthBias, GL.CullFace(Front/Back) is SetCulling(Cull_CW/Cull_CCW) (UZDoom's
** culling mode is the front face winding; Helion culls back faces with CCW front faces), GL.Enable/Disable(Blend)
** and GL.BlendFunc are the custom blend (SetCustomBlend / DisableBlend), GL.ActiveTexture needs no
** equivalent (the texture unit table of Shader/TextureUnits.h is addressed by unit), GL.MemoryBarrier
** (FramebufferBarrierBit) is not needed: render targets are separate backend passes.
** RenderProfiler / Stopwatch / logging are not ported.
**
** UZDoom-only: the entity milestone's subsystems (see LegacyWorldRenderer.h) run around Helion's order: the gather of
** the tick feeds EntityRenderer / EntityModelRenderer; every frame QueueFrame refills the sorted bucket (D10) that is
** drawn after the OIT composite, the opaque models and decals follow the opaque entities.
*/

#include "OpenGL/Renderers/Legacy/World/LegacyWorldRenderer.h"
#include "OpenGL/Shader/ProgramRegistry.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"
#include "OpenGL/Texture/Legacy/GLTextureArrayBuilder.h"
#include "Common/Shared/Frustum.h"
#include "BindTextures.h"
#include "HelionCVars.h"
#include "Renderer.h"
#include "World/Blockmap/RenderBlockMap.h"
#include "World/Static/StaticDataApplier.h"
#include "World/WorldStatic.h"
#include "Util/Constants.h"
#include "Util/MathHelper.h"

#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Static/StaticCacheGeometryRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderWorldDataManager.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityModelRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Entities/WeaponRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Entities/CoronaRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/DecalRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Data/SortedTranslucentRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Primitives/PrimitiveWorldRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalDriver.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/FloodFill/FloodFillRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Sky/ISkyComponent.h"
#include "OpenGL/Framebuffer/GLFramebuffer.h"
#include "OpenGL/Framebuffer/OitFrameBuffer.h"
#include "OpenGL/Framebuffer/PlaneClipFrameBuffer.h"

#include "g_levellocals.h"
#include "actor.h"
#include "p_local.h"
#include "a_sharedglobal.h"
#include "d_player.h"
#include "v_video.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "hwrenderer/data/hw_viewpointuniforms.h"
#include "r_utility.h"
#include "hw_cvars.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

using namespace Helion::Geometry;
using Helion::Render::BindTextures;
using Helion::Render::Renderer;
using Helion::Render::OpenGL::Shader::HelionProgramRegistry;
using Helion::Render::OpenGL::Shader::ProgramType;
using Helion::World::Geometry::Sectors::TransferHeightView;
using Data::RenderDataStyle;

namespace
{

FRenderState& State()
{
	return *screen->RenderState();
}

// GL.Enable(EnableCap.PolygonOffsetFill) / GL.Disable
void EnablePolygonOffset(bool enable)
{
	if (!enable)
		State().ClearDepthBias();
}

void SetPolygonOffsetFloodFill()
{
	// ShaderVars.ReversedZ is not ported (plan D11).
	State().SetPolygonOffset(0.05f, 1.0f);
}

// UZDoom (plan M5.2): true while the view is drawn through an odd number of mirrors (FPortalSceneState::isMirrored): the
// image is mirrored, so the faces that are culled swap.
bool s_windingFlipped = false;

// GL.CullFace(TriangleFace.Front / Back) with culling enabled.
void CullFrontFaces()
{
	State().SetCulling(s_windingFlipped ? Cull_CCW : Cull_CW);
}

void CullBackFaces()
{
	State().SetCulling(s_windingFlipped ? Cull_CW : Cull_CCW);
}

// The program of a material shader index (FMaterial::GetShaderIndex); nullptr for the default material, which is drawn
// through the array texture programs.
Entities::EntityProgram* GetEntityMaterialProgram(int materialShaderIndex)
{
	if (materialShaderIndex <= 0 || materialShaderIndex >= Shader::UzdoomContract::MaterialShaderCount())
		return nullptr;
	return HelionProgramRegistry::Get<Entities::EntityProgram>(ProgramType::EntityMaterial, materialShaderIndex, NORMAL_PASS);
}

Entities::ModelProgram* GetModelProgram(int materialShaderIndex)
{
	if (materialShaderIndex < 0 || materialShaderIndex >= Shader::UzdoomContract::MaterialShaderCount())
		return nullptr;
	return HelionProgramRegistry::Get<Entities::ModelProgram>(ProgramType::Model, materialShaderIndex, NORMAL_PASS);
}

}

LegacyWorldRenderer::LegacyWorldRenderer(Texture::Legacy::LegacyGLTextureManager& textureManager) :
	m_textureManager(textureManager),
	m_vanillaRender(gl_vanillarender)
{
	m_interpolationProgram = HelionProgramRegistry::Get<InterpolationShader>(ProgramType::InterpolationArray);
	m_interpolationTransparentProgram = HelionProgramRegistry::Get<InterpolationTransparentShader>(ProgramType::InterpolationTransparent);
	m_interpolationCompositeProgram = HelionProgramRegistry::Get<InterpolationCompositeShader>(ProgramType::InterpolationComposite);
	m_interpolationPlaneClipAlphaProgram = HelionProgramRegistry::Get<InterpolationPlaneClipAlphaShader>(ProgramType::InterpolationPlaneClipAlpha);
	m_interpolationPlaneClipMrtProgram = HelionProgramRegistry::Get<InterpolationPlaneClipShaderMrt>(ProgramType::InterpolationPlaneClipMrt);
	m_interpolationWallClipShader = HelionProgramRegistry::Get<InterpolationWallClipShader>(ProgramType::InterpolationWallClip);
	m_interpolationWallClipAlphaProgram = HelionProgramRegistry::Get<InterpolationWallClipAlphaShader>(ProgramType::InterpolationWallClipAlpha);
	m_staticProgram = HelionProgramRegistry::Get<StaticShader>(ProgramType::StaticArray);
	m_staticPlaneClipProgram = HelionProgramRegistry::Get<StaticPlaneClipShader>(ProgramType::StaticPlaneClip);
	m_staticPlaneClipAlphaProgram = HelionProgramRegistry::Get<StaticPlaneClipAlphaShader>(ProgramType::StaticPlaneClipAlpha);
	m_staticPlaneClipMrtProgram = HelionProgramRegistry::Get<StaticPlaneClipShaderMrt>(ProgramType::StaticPlaneClipMrt);
	m_staticWallClipProgram = HelionProgramRegistry::Get<StaticWallClipShader>(ProgramType::StaticWallClip);
	m_staticWallClipAlphaProgram = HelionProgramRegistry::Get<StaticWallClipAlphaShader>(ProgramType::StaticWallClipAlpha);
	m_staticTransparentProgram = HelionProgramRegistry::Get<StaticTransparentShader>(ProgramType::StaticTransparent);
	m_staticCompositeProgram = HelionProgramRegistry::Get<StaticCompositeShader>(ProgramType::StaticComposite);

	m_primitiveRenderer = new Primitives::PrimitiveWorldRenderer();
	m_worldDataManager = new Data::RenderWorldDataManager(m_interpolationProgram, [this]() { OnDynamicDraw(); });
	m_geometryRenderer = new Geometry::GeometryRenderer(&textureManager, m_interpolationProgram, m_staticProgram, m_worldDataManager, &m_viewClipper, &m_viewClipperPrev);
	Data::RenderWorldData::MaterialDraw = [this](Texture::Legacy::GLLegacyTexture* texture, const std::function<void()>& draw) { return DrawMaterialGeometry(texture, draw); };
	m_oitFrameBuffer = new Framebuffer::OitFrameBuffer();

	// UZDoom-only subsystems (plan M4).
	m_sortedRenderer = new Data::SortedTranslucentRenderer();
	m_hudSortedRenderer = new Data::SortedTranslucentRenderer();
	m_modelRenderer = new Entities::EntityModelRenderer(GetModelProgram);
	m_modelRenderer->SetSortedRenderer(m_sortedRenderer);
	m_entityRenderer = new Entities::EntityRenderer(textureManager,
		HelionProgramRegistry::Get<Entities::EntityProgram>(ProgramType::EntityMain),
		HelionProgramRegistry::Get<Entities::EntityTransparentProgram>(ProgramType::EntityTransparent),
		HelionProgramRegistry::Get<Entities::EntityCompositeProgram>(ProgramType::EntityComposite),
		HelionProgramRegistry::Get<Entities::EntityFuzzRefractionProgram>(ProgramType::EntityFuzzRefraction));
	m_entityRenderer->SetSortedRenderer(m_sortedRenderer);
	m_entityRenderer->SetModelRenderer(m_modelRenderer);
	m_entityRenderer->SetMaterialProgramLookup(GetEntityMaterialProgram);
	m_weaponRenderer = new Entities::WeaponRenderer(textureManager, HelionProgramRegistry::Get<Entities::EntityProgram>(ProgramType::EntityMain), m_modelRenderer);
	m_weaponRenderer->SetSortedRenderer(m_hudSortedRenderer);
	m_weaponRenderer->SetMaterialProgramLookup(GetEntityMaterialProgram);
	m_decalRenderer = new Geometry::DecalRenderer(HelionProgramRegistry::Get<Geometry::DecalProgram>(ProgramType::Decal), &textureManager, m_sortedRenderer);
	m_coronaRenderer = new Entities::CoronaRenderer(HelionProgramRegistry::Get<Entities::CoronaProgram>(ProgramType::Corona), &textureManager);

	// UZDoom (plan M5.2): the portal driver; Renderer binds Hooks.RenderView.
	m_portalDriver = new Geometry::Portals::PortalDriver(m_sortedRenderer);
	m_portalDriver->Hooks.RestoreView = [this](const Geometry::Portals::PortalFrameView& view) { RestorePortalView(view); };
	m_portalDriver->Hooks.DrawHorizon = [this](const Geometry::Portals::HorizonDraw& draw) { DrawHorizon(draw); };
	m_portalDriver->Hooks.DrawSectorSky = [this](sector_t* sector, int plane, const FRenderViewpoint&) { DrawSectorSky(sector, plane); };
}

void LegacyWorldRenderer::OnDynamicDraw()
{
	// m_renderProfiler.DrawCounts.GeometryDynamic++ (profiler not ported)
}

LegacyWorldRenderer::~LegacyWorldRenderer()
{
	ReleaseUnmanagedResources();
}

void LegacyWorldRenderer::Dispose()
{
	ReleaseUnmanagedResources();
}

void LegacyWorldRenderer::UpdateToNewWorld(FLevelLocals* world)
{
	m_lastRenderedWorld = world;
	m_lastValidatedTick = -1;

	Helion::World::WorldStatic::Sector3D = false;
	for (unsigned i = 0; i < world->sectors.Size(); i++)
	{
		if (world->sectors[i].e != nullptr && world->sectors[i].e->XFloor.ffloors.Size() != 0)
		{
			Helion::World::WorldStatic::Sector3D = true;
			break;
		}
	}

	if (m_vanillaRender && m_planeClipFrameBuffer == nullptr)
		m_planeClipFrameBuffer = new Framebuffer::PlaneClipFrameBuffer();
	else if (!m_vanillaRender)
	{
		delete m_planeClipFrameBuffer;
		m_planeClipFrameBuffer = nullptr;
	}

	if (m_vanillaRender && m_wallClipFrameBuffer == nullptr)
		m_wallClipFrameBuffer = new Framebuffer::PlaneClipFrameBuffer();
	else if (!m_vanillaRender)
	{
		delete m_wallClipFrameBuffer;
		m_wallClipFrameBuffer = nullptr;
	}

	if (!m_spritesCached)
	{
		SpriteTextureBuckets = Texture::Legacy::GLTextureArrayBuilder::BuildSprites(m_textureManager, sprites);
		m_spritesCached = true;
	}

	// UZDoom (plan M5.2): portal planes are never in the static cache (SD_Portal); has to be set before it is built.
	for (unsigned i = 0; i < world->sectors.Size(); i++)
	{
		sector_t* sector = &world->sectors[i];
		if (sector->ValidatePortal(sector_t::floor) != nullptr || sector->GetReflect(sector_t::floor) > 0)
			Helion::World::Static::StaticDataApplier::SetSectorDynamic(sector, Helion::World::Static::SectorPlanes_Floor, SD_Portal);
		if (sector->ValidatePortal(sector_t::ceiling) != nullptr || sector->GetReflect(sector_t::ceiling) > 0)
			Helion::World::Static::StaticDataApplier::SetSectorDynamic(sector, Helion::World::Static::SectorPlanes_Ceiling, SD_Portal);
	}
	BuildPortalCandidates(world);

	m_geometryRenderer->UpdateTo(world);
	m_entityRenderer->UpdateTo(world);
	m_modelRenderer->Clear();
	m_decalRenderer->Clear();
	m_coronaRenderer->Clear();
	m_sideDecalStamp.assign(world->sides.Size(), 0);
	m_decalStamp = 0;

	m_previousWorld = world;
	m_lastTicker = -1;
	m_pixelGapCorrection = gl_pixelgapcorrection;
	m_lastTransferHeightsView = TransferHeightView::Middle;

	// Sector bounds for the dynamic sector distance test (Helion: the island bounding box).
	m_sectorBounds.resize(world->sectors.Size());
	for (unsigned i = 0; i < world->sectors.Size(); i++)
	{
		sector_t* sector = &world->sectors[i];
		Box2D box(Vec2D(DBL_MAX, DBL_MAX), Vec2D(-DBL_MAX, -DBL_MAX));
		for (line_t* line : sector->Lines)
		{
			for (vertex_t* v : { line->v1, line->v2 })
			{
				box.Min.X = std::min(box.Min.X, v->fX());
				box.Min.Y = std::min(box.Min.Y, v->fY());
				box.Max.X = std::max(box.Max.X, v->fX());
				box.Max.Y = std::max(box.Max.Y, v->fY());
			}
		}
		m_sectorBounds[i] = box;
	}
	m_sectorCheckCount.assign(world->sectors.Size(), 0);
	m_sideCheckCount.assign(world->sides.Size(), 0);

	m_bspHeuristicsImpl.Start(world);
	m_bspHeuristicsImpl.SetPortalCandidates(m_portalSegs, m_portalSubsectors);
	m_bspHeuristics = &m_bspHeuristicsImpl;
	m_smoothedBspTimeUs = -1;
	m_bspTimeWindow.clear();
	m_aboveThresholdCount = 0;
	m_belowThresholdCount = 0;
	m_lastProcessedId = 0;
	m_lastBspHeuristicTick = -1;
}

bool LegacyWorldRenderer::NeedsRenderTickChange(FLevelLocals* world, TransferHeightView view) const
{
	return m_lastTicker != world->maptime || m_renderStatic != m_lastRenderStatic || m_lastTransferHeightsView != view;
}

void LegacyWorldRenderer::IterateBlockmap(FLevelLocals* world)
{
	Box2D box(Vec2D(m_renderData.ViewPosInterpolated.X - m_renderData.MaxDistance, m_renderData.ViewPosInterpolated.Y - m_renderData.MaxDistance),
		Vec2D(m_renderData.ViewPosInterpolated.X + m_renderData.MaxDistance, m_renderData.ViewPosInterpolated.Y + m_renderData.MaxDistance));

	Vec2D occluder = m_renderData.OccludePos.value_or(Vec2D(0, 0));
	bool occlude = m_renderData.OccludePos.has_value();

	auto* blockmap = world->renderBlockmap;
	auto it = blockmap->CreateBoxIteration(m_renderData.ViewPosInterpolated.X, m_renderData.ViewPosInterpolated.Y, m_renderData.MaxDistance);
	int dimension = blockmap->Dimension;
	(void)box;
	for (int by = it.BlockStartY; by <= it.BlockEndY; by++)
	{
		for (int bx = it.BlockStartX; bx <= it.BlockEndX; bx++)
		{
			int index = by * it.Width + bx;
			if (occlude && !BlockInView(bx, by, dimension, blockmap->OriginX, blockmap->OriginY, occluder, m_renderData.ViewDirection))
				continue;

			RenderSectors(world, index);

			if (m_renderStatic)
				RenderSides(world, index);

			int renderIndex = 0;
			for (AActor* entity = blockmap->HeadRenderEntities[index]; entity != nullptr; entity = entity->renderBlockNext)
			{
				renderIndex = renderIndex % EntityRenderIndexMax;
				RenderEntity(world, entity, renderIndex++);
			}
		}
	}

	m_entityRenderer->RenderParticles(world, *m_currentRenderInfo, 0);

	m_lastTicker = world->maptime;
}

void LegacyWorldRenderer::SetupRenderData(FLevelLocals* world, Shared::RenderInfo& renderInfo)
{
	m_renderData.ViewerEntity = renderInfo.ViewerEntity;
	m_renderData.ViewPosInterpolated = Vec2D(renderInfo.Camera->PositionInterpolated.X, renderInfo.Camera->PositionInterpolated.Y);
	m_renderData.ViewPosInterpolated3D = Vec3D(renderInfo.Camera->PositionInterpolated.X, renderInfo.Camera->PositionInterpolated.Y, renderInfo.Camera->PositionInterpolated.Z);
	m_renderData.ViewPos3D = Vec3D(renderInfo.Camera->Position.X, renderInfo.Camera->Position.Y, renderInfo.Camera->Position.Z);
	m_renderData.ViewDirection = Vec2D(renderInfo.Camera->Direction.X, renderInfo.Camera->Direction.Y);
	// m_renderData.ViewIsland has no UZDoom counterpart (see RenderBlockMapData.h).

	m_viewerEntity = renderInfo.ViewerEntity;
	m_renderData.CheckCount = ++Helion::World::WorldStatic::CheckCounter;

	m_renderData.MaxDistance = renderInfo.Uniforms.MaxDistance;

	m_renderData.MaxDistanceSquared = (double)m_renderData.MaxDistance * m_renderData.MaxDistance;
	m_renderData.OccludePos = m_occlude ? std::optional<Vec2D>(m_occludeViewPos) : std::nullopt;
}

bool LegacyWorldRenderer::BlockInView(int x, int y, int dimension, double originX, double originY, const Vec2D& viewPos, const Vec2D& viewDirection)
{
	double minX = x * dimension + originX;
	double minY = y * dimension + originY;
	double maxX = minX + dimension;
	double maxY = minY + dimension;

	double vx = viewDirection.X;
	double vy = viewDirection.Y;

	double dxMin = minX - viewPos.X;
	double dxMax = maxX - viewPos.X;
	double dyMin = minY - viewPos.Y;
	double dyMax = maxY - viewPos.Y;

	// Avoid branch misprediction by forcing computation with bitwise or
	return ((dxMin * vx + dyMin * vy) >= 0) | ((dxMax * vx + dyMax * vy) >= 0) | ((dxMin * vx + dyMax * vy) >= 0) | ((dxMax * vx + dyMin * vy) >= 0);
}

void LegacyWorldRenderer::RenderSectors(FLevelLocals* world, int blockIndex)
{
	auto& sectorList = world->renderBlockmap->DynamicSectors[blockIndex];
	if (sectorList.Size() == 0)
		return;

	// UZDoom has no islands: the check count and the bounding box are per sector (the island of a sector
	// is the sector itself), so the island's BlockmapCount / ParentIsland checks collapse into the sector's CheckCount.
	for (unsigned i = 0; i < sectorList.Size(); i++)
	{
		sector_t* sector = sectorList[i];
		int id = sector->Index();
		if (m_sectorCheckCount[id] == m_renderData.CheckCount)
			continue;

		const Box2D& bounds = m_sectorBounds[id];
		double dx1 = std::max(bounds.Min.X - m_renderData.ViewPosInterpolated.X, std::max(0.0, m_renderData.ViewPosInterpolated.X - bounds.Max.X));
		double dy1 = std::max(bounds.Min.Y - m_renderData.ViewPosInterpolated.Y, std::max(0.0, m_renderData.ViewPosInterpolated.Y - bounds.Max.Y));
		if (dx1 * dx1 + dy1 * dy1 <= m_renderData.MaxDistanceSquared)
		{
			m_geometryRenderer->RenderSectorGeometry(sector, m_renderData.ViewPos3D, m_renderData.ViewPosInterpolated3D);
			m_sectorCheckCount[id] = m_renderData.CheckCount;
		}
	}
}

void LegacyWorldRenderer::RenderSides(FLevelLocals* world, int blockIndex)
{
	auto& sides = world->renderBlockmap->DynamicSides[blockIndex];
	if (sides.Size() == 0)
		return;

	// DynamicSides are scrolling textures and should not setup cover walls.
	m_geometryRenderer->SetBufferCoverWall(false);
	for (unsigned i = 0; i < sides.Size(); i++)
	{
		side_t* side = sides[i];
		int id = side->Index();
		if (m_sideCheckCount[id] == m_renderData.CheckCount)
			continue;

		line_t* line = side->linedef;
		side_t* partner = line->sidedef[line->sidedef[0] == side ? 1 : 0];
		if (Helion::World::Static::StaticDataApplier::IsMoving(side->sector) ||
			(partner != nullptr && Helion::World::Static::StaticDataApplier::IsMoving(partner->sector)))
			continue;

		m_sideCheckCount[id] = m_renderData.CheckCount;
		if (side->Flags & WALLF_POLYOBJ)
		{
			m_geometryRenderer->RenderPolyobjectWall(PolyobjectSideSector(world, side, nullptr), side, m_renderData.ViewPos3D, m_renderData.ViewPosInterpolated3D);
			continue;
		}
		m_geometryRenderer->RenderSectorWall(side->sector, line, m_renderData.ViewPos3D, m_renderData.ViewPosInterpolated3D);
	}
	m_geometryRenderer->SetBufferCoverWall(true);
}

// UZDoom (plan D27): the sector a polyobject side is lit and bounded by. The classic renderer splits the sides at the subsectors they
// cross (FPolyNode) and draws each piece in its subsector's sector, a piece lying on a partition line going to the side of the
// polyobject's center (po_man.cpp SplitPoly). So the sector is the one just inside the polyobject from the middle of the line, toward
// its center, for both sides of the line: a door polyobject in a doorway is lit by the doorway, not by the room it faces. One sector
// per side keeps the side a single dynamic wall, the same in the static and the BSP walk. poly may be null (looked up).
sector_t* LegacyWorldRenderer::PolyobjectSideSector(FLevelLocals* world, side_t* side, FPolyObj* poly)
{
	if (poly == nullptr)
	{
		for (FPolyObj& candidate : world->Polyobjects)
		{
			if (candidate.Sidedefs.Find(side) < candidate.Sidedefs.Size())
			{
				poly = &candidate;
				break;
			}
		}
	}

	line_t* line = side->linedef;
	DVector2 mid = line->v1->fPos() + line->Delta() * 0.5;
	DVector2 inward = poly != nullptr ? poly->CenterSpot.pos - mid : DVector2(-line->Delta().Y, line->Delta().X);
	if (inward.LengthSquared() > 0)
		inward.MakeUnit();
	return world->PointInRenderSubsector(mid + inward)->sector;
}

void LegacyWorldRenderer::RenderEntity(FLevelLocals* world, AActor* entity, int renderIndex)
{
	// Frame.IsInvisible is the sprite 0 (TNT1) check, Flags.Invisible() is RF_INVISIBLE, Flags.NoSector() is MF_NOSECTOR.
	if (entity->sprite == 0 || (entity->renderflags & RF_INVISIBLE) || (entity->flags & MF_NOSECTOR) ||
		(!m_chaseCam && entity == m_viewerEntity) || entity->RenderStyle.BlendOp == STYLEOP_None)
		return;

	if (m_renderData.OccludePos.has_value())
	{
		double entityDx = entity->X() - m_renderData.OccludePos->X;
		double entityDy = entity->Y() - m_renderData.OccludePos->Y;
		double dot = entityDx * m_renderData.ViewDirection.X + entityDy * m_renderData.ViewDirection.Y;
		if (dot < 0)
			return;
	}

	double dx = std::max(entity->X() - m_renderData.ViewPosInterpolated.X, std::max(0.0, m_renderData.ViewPosInterpolated.X - entity->X()));
	double dy = std::max(entity->Y() - m_renderData.ViewPosInterpolated.Y, std::max(0.0, m_renderData.ViewPosInterpolated.Y - entity->Y()));
	entity->renderDistanceSquared = dx * dx + dy * dy;
	if (entity->renderDistanceSquared > m_renderData.MaxDistanceSquared)
		return;

	entity->lastRenderGametick = world->maptime;
	if (m_coronaRenderer->IsCorona(entity))
		m_coronaRenderer->AddCorona(entity);
	m_entityRenderer->RenderEntity(entity, m_renderData.ViewPosInterpolated, renderIndex);
}

void LegacyWorldRenderer::BuildViewLights(FLevelLocals* world, const Shared::RenderInfo& renderInfo)
{
	while ((int)m_viewLights.size() <= m_viewLightsDepth)
		m_viewLights.push_back(std::make_unique<Lights::ViewLights>());

	Lights::ViewLights& lights = *m_viewLights[m_viewLightsDepth];
	Lights::ClusterLightBuffer::ViewParams view;
	view.ViewportX = renderInfo.Viewport.left;
	view.ViewportY = renderInfo.Viewport.top;
	view.Width = renderInfo.Viewport.width;
	view.Height = renderInfo.Viewport.height;
	view.ViewMatrix = renderInfo.Viewpoint->ViewMatrix.get();
	view.ProjectionMatrix = renderInfo.Viewpoint->ProjectionMatrix.get();
	view.NearZ = screen->GetZNear();
	view.FarZ = screen->GetZFar();
	if (renderInfo.Uniforms.MaxDistance > 0)
		view.FarZ = std::min(view.FarZ, (float)renderInfo.Uniforms.MaxDistance);
	view.PortalGroup = renderInfo.ViewSector != nullptr ? renderInfo.ViewSector->PortalGroup : 0;

	// The classic renderer: no dynamic lights in a fullbright scene (isFullbrightScene); r_dynlights is tested by the light list.
	lights.Build(world, view, !renderInfo.DrawFullBright);
	Lights::ViewLights::SetCurrent(&lights);
	m_lightAdditiveSurfaces = world->lightadditivesurfaces;
}

void LegacyWorldRenderer::RenderAllAlpha(IStyleRenderer& styleRenderer, RenderProgramBase& program)
{
	using Data::RenderDataStyle;
	for (RenderDataStyle style : { RenderDataStyle::Translucent, RenderDataStyle::Add, RenderDataStyle::ColorAdd })
	{
		program.ClusterLightsOff(style == RenderDataStyle::Add && !m_lightAdditiveSurfaces);
		styleRenderer.Render(style);
	}
	program.ClusterLightsOff(false);
}

void LegacyWorldRenderer::PerformRender(FLevelLocals* world, Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer)
{
	m_currentRenderInfo = &renderInfo;
	FRenderState& state = State();

	// UZDoom (plan D25): the light -> cluster assignment of the view, before its first program is bound (the compute path
	// leaves the render state in table shader mode).
	BuildViewLights(world, renderInfo);
	// UZDoom (plan M5.2): a portal view drawn through a mirror portal has the reversed winding.
	const bool portalView = m_portalClip != nullptr;
	s_windingFlipped = portalView && m_portalClip->WindingFlipped();

	// UZDoom: the state Helion's GL context leaves set up for the world pass.
	state.SetScissor(renderInfo.Viewport.left, renderInfo.Viewport.top, renderInfo.Viewport.width, renderInfo.Viewport.height);
	state.EnableDepthTest(true);
	state.SetDepthMask(true);
	state.SetDepthFunc(DF_Less);
	state.SetColorMask(true);
	state.EnableStencil(true);
	state.SetStencil(0, SOP_Keep, SF_AllOn);
	CullBackFaces();
	ResetBlendEquations();

	// If the transfer height view is not the middle then the cached static geometry cannot be used.
	// Render all sectors dynamically instead.
	m_lastRenderStatic = m_renderStatic;
	m_renderStatic = gl_rendermode != 1 && renderInfo.TransferHeightView == TransferHeightView::Middle && !m_lastUseBsp;
	m_postProcessingEffects = true;
	m_chaseCam = renderInfo.ShowViewer;

	if (portalView)
	{
		// UZDoom: portal views walk the BSP like the classic renderer did (the portal clip, map sections and the coverage of sector
		// stacks act on the walk); the heuristic is the main view's.
		m_renderStatic = false;
	}
	else
	{
		// UZDoom: the automap marker walk of the player's view (Helion: AutomapMarker.AddPosition every tick), which also times
		// the BSP walk for the adaptive mode. An out of bounds view with the radar clipper marks in its own BSP walk instead.
		if (renderInfo.MainView && m_lastBspHeuristicTick != world->maptime)
		{
			m_lastBspHeuristicTick = world->maptime;
			bool radar = r_viewpoint.bDoOob && r_radarclipper && !(world->flags3 & LEVEL3_NOFOGOFWAR);
			m_bspHeuristicsImpl.Process(Vec3D(renderInfo.Camera->PositionInterpolated.X, renderInfo.Camera->PositionInterpolated.Y, renderInfo.Camera->PositionInterpolated.Z),
				renderInfo.Uniforms.MvpNoPitch, world->maptime, !radar, !r_viewpoint.bDoOob);
		}

		if (renderInfo.TransferHeightView == TransferHeightView::Middle)
		{
			m_lastUseBsp = UseBspBasedOnHeuristic(world, renderInfo);
			m_renderStatic = !m_lastUseBsp;
		}

		// UZDoom: the radar clipper, r_dithertransparency of an out of bounds camera and r_distance_cull_type force BSP mode (plan D27).
		if (renderInfo.ForceBsp)
		{
			m_lastUseBsp = true;
			m_renderStatic = false;
		}

		// UZDoom (FCanvasTextureInfo::UpdateAll): the static mode binds the camera textures of the whole map, so the camera
		// textures the walk of the main view reached are the seen ones (DrawMaterialGeometry marks them in the BSP mode).
		if (m_renderStatic && renderInfo.MainView && m_bspHeuristicsImpl.Valid())
		{
			for (FCanvasTexture* canvas : m_bspHeuristicsImpl.SeenCanvasTextures())
				canvas->NeedUpdate();
		}
	}

	// Portal views never reuse the gather of an earlier call (it was another view).
	bool renderTickChange = NeedsRenderTickChange(world, renderInfo.TransferHeightView) || portalView;
	m_lastTransferHeightsView = renderInfo.TransferHeightView;

	// UZDoom (plan D14): catches direct writes that do not raise the world events. Has to run before anything is
	// built from the sector / side state.
	ValidateSignatures(world);

	m_decalRenderer->SetVanillaRender(m_vanillaRender && !portalView);
	Clear(world, renderInfo, renderTickChange);

	m_geometryRenderer->SetRenderMode(m_renderStatic ? Geometry::GeometryRenderMode::Dynamic : Geometry::GeometryRenderMode::All, renderInfo.TransferHeightView, renderTickChange);

	Dimension dimension(renderInfo.Viewport.width, renderInfo.Viewport.height);
	if (framebuffer == nullptr || !framebuffer->HasDepthTexture())
		I_FatalError("Framebuffer must have a depth texture.");

	m_oitFrameBuffer->CreateOrUpdate(dimension, framebuffer->DepthTexture);

	bool prevDownscale = m_downscaleVanillaBuffer;
	m_downscaleVanillaBuffer = gl_vanillarender_downscale > 1;
	SetupClipBuffers(framebuffer, dimension, prevDownscale != m_downscaleVanillaBuffer);

	SetOccludePosition(Vec3D(renderInfo.Camera->PositionInterpolated.X, renderInfo.Camera->PositionInterpolated.Y, renderInfo.Camera->PositionInterpolated.Z),
		renderInfo.Camera->YawRadians, renderInfo.Camera->PitchRadians, m_occlude, m_occludeViewPos);

	// UZDoom (plan M5.2, FPortalSceneState::StartFrame): the portals of this view are collected by the BSP walk when it runs this frame,
	// otherwise from the portal candidates.
	Geometry::Portals::PortalFrameView portalFrame;
	portalFrame.Level = world;
	portalFrame.Viewpoint = &r_viewpoint;
	portalFrame.Clip = m_portalClip;
	portalFrame.Mvp = renderInfo.Uniforms.Mvp;
	portalFrame.PixelStretch = renderInfo.PixelStretch;
	portalFrame.Culling = s_windingFlipped ? Cull_CW : Cull_CCW;
	portalFrame.RenderInfo = &renderInfo;
	m_portalDriver->SetLevel(world);
	m_portalDriver->StartFrame(portalFrame);

	bool collectInWalk = renderTickChange && !m_renderStatic;
	if (renderTickChange)
	{
		m_geometryRenderer->SetPortalDriver(collectInWalk ? m_portalDriver : nullptr);
		GatherWorld(world, renderInfo);
		m_geometryRenderer->SetPortalDriver(nullptr);
	}
	if (!collectInWalk)
		CollectPortals(world, renderInfo);

	PopulatePrimitives(world);
	QueueFrame(world, renderInfo);

	m_geometryRenderer->RenderSkies(renderInfo, m_renderStatic, !m_bspHeuristicsImpl.Valid() || m_bspHeuristicsImpl.SkyVisible());
	RenderFloodFill(renderInfo);

	if (m_renderStatic)
		m_geometryRenderer->RenderStaticSkies(renderInfo);

	// UZDoom: the vanilla clip buffers clear the depth buffer, which a portal view must not do.
	RenderOpaque(renderInfo, framebuffer, m_vanillaRender && !portalView);

	// UZDoom (plan M5.2, FPortalSceneState::EndFrame): the portals of this view, after its opaque geometry and before its
	// translucent geometry.
	if (m_portalDriver->EndFrame())
	{
		// The portal views gathered into the same buffers: gather this view again for its transparent pass, without portals.
		m_lastTicker = -1;
		m_portalDriver->SetDryRun(true);
		Clear(world, renderInfo, true);
		m_geometryRenderer->SetRenderMode(m_renderStatic ? Geometry::GeometryRenderMode::Dynamic : Geometry::GeometryRenderMode::All, renderInfo.TransferHeightView, true);
		SetOccludePosition(Vec3D(renderInfo.Camera->PositionInterpolated.X, renderInfo.Camera->PositionInterpolated.Y, renderInfo.Camera->PositionInterpolated.Z),
			renderInfo.Camera->YawRadians, renderInfo.Camera->PitchRadians, m_occlude, m_occludeViewPos);
		GatherWorld(world, renderInfo);
		PopulatePrimitives(world);
		QueueFrame(world, renderInfo);
		m_portalDriver->SetDryRun(false);
	}

	RenderTransparent(renderInfo, framebuffer);
	m_sortedRenderer->Render(renderInfo);
	m_coronaRenderer->Render(renderInfo);
	EndWorldPass();
}

void LegacyWorldRenderer::GatherWorld(FLevelLocals* world, Shared::RenderInfo& renderInfo)
{
	SetupRenderData(world, renderInfo);
	m_entityRenderer->SetRenderInfo(renderInfo);

	// UZDoom (r_dithertransparency): the main view's walk collects the dither targets and flags what hides them for the next gather.
	const bool mainWalk = m_portalClip == nullptr && renderInfo.MainView;
	if (mainWalk)
		m_dither.BeginWalk(!m_renderStatic && DitherTransparency::Enabled(r_viewpoint));
	m_geometryRenderer->SetDither(mainWalk && m_dither.Active());

	if (m_renderStatic)
		IterateBlockmap(world);
	else
		TraverseBsp(world, renderInfo);

	m_geometryRenderer->SetDither(false);
	if (mainWalk)
		m_dither.EndWalk(world, r_viewpoint);
}

void LegacyWorldRenderer::RenderOpaque(Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer, bool vanilla)
{
	if (!vanilla)
	{
		m_interpolationProgram->Bind();
		SetInterpolationUniforms(m_interpolationProgram, renderInfo);
		m_interpolationProgram->VertexGapClampUV(m_pixelGapCorrection);
		m_worldDataManager->RenderWalls();
		m_interpolationProgram->VertexGapClampUV(false);
		m_worldDataManager->RenderFlats();

		if (m_renderStatic)
		{
			m_staticProgram->Bind();
			SetStaticUniforms(m_staticProgram, renderInfo);
			m_staticProgram->VertexGapClampUV(m_pixelGapCorrection);
			m_geometryRenderer->RenderStaticGeometryWalls();
			m_staticProgram->VertexGapClampUV(false);
			m_geometryRenderer->RenderStaticGeometryFlats();
		}

		RenderTwoSidedMiddleWalls(renderInfo);
		m_entityRenderer->RenderOpaque(renderInfo);
		m_modelRenderer->RenderOpaque(renderInfo);
		m_decalRenderer->RenderOpaque(renderInfo);
		m_primitiveRenderer->RenderAll(renderInfo);
		return;
	}

	m_interpolationProgram->Bind();
	SetInterpolationUniforms(m_interpolationProgram, renderInfo);
	m_interpolationProgram->VertexGapClampUV(m_pixelGapCorrection);
	m_worldDataManager->RenderWalls();

	// UZDoom: the plane clip buffer is never MRT (Framebuffer/PlaneClipFrameBuffer.h), so the flats are always drawn here; Helion
	// draws them here only when the vanilla buffer is downscaled and otherwise into the colour attachment of the plane clip pass.
	m_interpolationProgram->VertexGapClampUV(false);
	m_worldDataManager->RenderFlats();

	if (m_renderStatic)
	{
		m_staticProgram->Bind();
		SetStaticUniforms(m_staticProgram, renderInfo);
		m_staticProgram->VertexGapClampUV(m_pixelGapCorrection);
		m_geometryRenderer->RenderStaticGeometryWalls();
		m_staticProgram->VertexGapClampUV(false);
		m_geometryRenderer->RenderStaticGeometryFlats();
	}

	RenderTwoSidedMiddleWalls(renderInfo);

	if (m_wallClipFrameBuffer != nullptr || m_planeClipFrameBuffer != nullptr)
		WriteSpriteClipBuffers(renderInfo, framebuffer);

	m_primitiveRenderer->RenderAll(renderInfo);

	// UZDoom: the decals (all of them, see DecalRenderer::RenderOpaque) are drawn while the depth of the walls is still there,
	// after the clear below they would show through the walls in front of them.
	m_decalRenderer->RenderOpaque(renderInfo);

	State().Clear(CT_Depth);

	if (m_primitiveRenderer->HasOpaque)
	{
		State().SetColorMask(false, false, false, false);
		m_primitiveRenderer->RenderOpaque(renderInfo);
		State().SetColorMask(true, true, true, true);
	}

	m_entityRenderer->RenderOpaque(renderInfo);
	m_modelRenderer->RenderOpaque(renderInfo);
}

// UZDoom-only: leaves the render state the way the rest of the frame (post processing, 2D) expects it.
void LegacyWorldRenderer::EndWorldPass()
{
	FRenderState& state = State();
	if (::Helion::Render::OpenGL::Shader::RenderProgram::Current() != nullptr)
		::Helion::Render::OpenGL::Shader::RenderProgram::Current()->Unbind();
	state.ClearCustomBlend();
	state.ClearDepthBias();
	state.SetCulling(Cull_None);
	state.SetDepthMask(true);
	state.SetColorMask(true);
}

void LegacyWorldRenderer::RenderFloodFill(Shared::RenderInfo& renderInfo)
{
	// Doom would draw middle textures over flood fill.
	// Setting the factor using PolygonOffset will push them further away in depth so middle textures are closer and render over.
	// Flood fill is pushed in world coordinates in the shader.
	SetPolygonOffsetFloodFill();
	m_geometryRenderer->RenderPortals(renderInfo);
	EnablePolygonOffset(false);
}

void LegacyWorldRenderer::WriteSpriteClipBuffers(Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer)
{
	Shared::RenderInfo* useRenderInfo = &renderInfo;
	if (renderInfo.Uniforms.DownScaleAmount > 1)
	{
		float downScaleAmount = renderInfo.Uniforms.DownScaleAmount;
		useRenderInfo = &m_downSizedRenderInfo;
		IntRect viewport = renderInfo.Viewport;
		viewport = { (int)(viewport.left / downScaleAmount), (int)(viewport.top / downScaleAmount), (int)(viewport.width / downScaleAmount), (int)(viewport.height / downScaleAmount) };
		m_downSizedRenderInfo = renderInfo;
		m_downSizedRenderInfo.Viewport = viewport;
		m_downSizedRenderInfo.Uniforms = Renderer::GetShaderUniforms(m_downSizedRenderInfo);

		State().SetViewport(viewport.left, viewport.top, viewport.width, viewport.height);
	}

	if (m_planeClipFrameBuffer != nullptr)
		WritePlaneClipData(m_planeClipFrameBuffer, *useRenderInfo, framebuffer, false);

	if (m_wallClipFrameBuffer != nullptr)
		WritePlaneClipData(m_wallClipFrameBuffer, *useRenderInfo, framebuffer, true);

	if (renderInfo.Uniforms.DownScaleAmount > 1)
		State().SetViewport(renderInfo.Viewport.left, renderInfo.Viewport.top, renderInfo.Viewport.width, renderInfo.Viewport.height);
}

void LegacyWorldRenderer::SetupClipBuffers(Framebuffer::GLFramebuffer* framebuffer, Dimension dimension, bool downscaleChanged)
{
	bool bind = false;
	if (m_planeClipFrameBuffer != nullptr)
	{
		Framebuffer::GLFramebuffer* colorBuffer = m_downscaleVanillaBuffer ? nullptr : framebuffer;
		m_planeClipFrameBuffer->CreateOrUpdate("PlaneClip", Framebuffer::PlaneClipType::Plane, dimension, colorBuffer, downscaleChanged);
		m_planeClipFrameBuffer->BindFrameBuffer();
		m_planeClipFrameBuffer->Clear();
		bind = true;
	}

	if (m_wallClipFrameBuffer != nullptr)
	{
		m_wallClipFrameBuffer->CreateOrUpdate("WallClip", Framebuffer::PlaneClipType::Wall, dimension, nullptr, false);
		m_wallClipFrameBuffer->BindFrameBuffer();
		m_wallClipFrameBuffer->Clear();
		bind = true;
	}

	if (bind)
		framebuffer->Bind();
}

void LegacyWorldRenderer::WritePlaneClipData(Framebuffer::PlaneClipFrameBuffer* planeClipFrameBuffer, Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer, bool walls)
{
	planeClipFrameBuffer->BindFrameBuffer();
	Framebuffer::PlaneClipFrameBuffer::StartRender();
	State().DisableBlend();

	if (walls)
	{
		m_interpolationWallClipShader->Bind();
		SetInterpolationUniforms(m_interpolationWallClipShader, renderInfo);
		State().SetCulling(Cull_None);
		m_worldDataManager->RenderCoverWalls();
		m_geometryRenderer->RenderWallClipPortals(renderInfo);
		CullBackFaces();
		// UZDoom: no Unbind (Helion's glUseProgram(0)) here: leaving the program mode of the render state also rebinds the scene
		// frame buffer, and the clip buffer is still the target of the next program.

		m_interpolationWallClipAlphaProgram->Bind();
		SetInterpolationUniforms(m_interpolationWallClipAlphaProgram, renderInfo);

		if (Helion::World::WorldStatic::Sector3D)
		{
			CullFrontFaces();
			m_worldDataManager->RenderMiddle3D();
			CullBackFaces();
			m_worldDataManager->RenderMiddle3D();
		}

		m_worldDataManager->RenderTwoSidedMiddleWalls();
	}
	else
	{
		// PlaneClipFrameBuffer is never MRT (see Framebuffer/PlaneClipFrameBuffer.h): the non MRT programs are used in both cases.
		// Helion picks the MRT program when the vanilla buffer is not downscaled, and (WorldStatic.Sector3D ? Alpha : Alpha)
		// for the downscaled one (both arms of that ternary are the same program).
		InterpolationShader* program = m_interpolationPlaneClipAlphaProgram;
		program->Bind();
		SetInterpolationUniforms(program, renderInfo);
		m_worldDataManager->RenderFlats();
	}

	if (m_renderStatic)
	{
		if (walls)
		{
			m_staticWallClipProgram->Bind();
			SetStaticUniforms(m_staticWallClipProgram, renderInfo);
			m_geometryRenderer->RenderStaticOneSidedCoverWalls();
			m_geometryRenderer->RenderStaticCoverWalls();
			CullFrontFaces();
			m_geometryRenderer->RenderStaticCoverWalls();
			CullBackFaces();

			m_staticWallClipAlphaProgram->Bind();
			SetStaticUniforms(m_staticWallClipAlphaProgram, renderInfo);

			if (Helion::World::WorldStatic::Sector3D)
			{
				CullFrontFaces();
				m_geometryRenderer->RenderStaticMiddle3D();
				CullBackFaces();
				m_geometryRenderer->RenderStaticMiddle3D();
			}

			m_geometryRenderer->RenderStaticTwoSidedWalls();
		}
		else
		{
			// Non MRT programs only (see above); Helion: downscaled ? (Sector3D ? Alpha : plain) : Mrt.
			StaticShader* program = Helion::World::WorldStatic::Sector3D ? static_cast<StaticShader*>(m_staticPlaneClipAlphaProgram) : static_cast<StaticShader*>(m_staticPlaneClipProgram);
			program->Bind();
			program->VertexGapClampUV(false);
			SetStaticUniforms(program, renderInfo);
			m_geometryRenderer->RenderStaticGeometryFlats();
		}
	}

	Framebuffer::PlaneClipFrameBuffer::UnbindFrameBuffer();
	framebuffer->Bind();
	planeClipFrameBuffer->BindPlaneTexture(walls ? BindTextures::WallClipTexture : BindTextures::PlaneClipTexture);
	ResetBlendEquations();
}

void LegacyWorldRenderer::RenderTransparent(Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer)
{
	bool hasEntityFuzzData = m_entityRenderer->HasDataToRenderByStyle(RenderDataStyle::Fuzzy);
	bool hasEntityAlphaData = m_entityRenderer->HasAlphaToRender();
	bool hasDynamicAlphaGeometry = m_worldDataManager->HasAlphaToRender();
	bool hasStaticAlphaGeometry = m_renderStatic && m_geometryRenderer->StaticRenderer().HasAlphaToRender();
	if (!hasEntityFuzzData && !hasEntityAlphaData && !hasDynamicAlphaGeometry && !hasStaticAlphaGeometry)
		return;

	SetPolygonOffsetFloodFill();
	m_oitFrameBuffer->StartRender();
	State().SetDepthMask(false);

	if (hasEntityAlphaData || hasEntityFuzzData)
		m_entityRenderer->RenderOitTransparentPass(renderInfo);

	m_oitFrameBuffer->BindTextures(BindTextures::AccumTexture, BindTextures::AccumCountTexture, BindTextures::FuzzTexture, BindTextures::OpaqueTexture, *framebuffer);

	if (hasEntityFuzzData)
	{
		if (m_postProcessingEffects)
		{
			ResetBlendEquations();
			framebuffer->Bind();
			// Refract pixels in the opaque framebuffer
			m_entityRenderer->RenderOitFuzzRefractionPass(renderInfo, false);
		}

		Framebuffer::OitFrameBuffer::SetBlendEquations();
		m_oitFrameBuffer->BindFrameBuffer();

		m_entityRenderer->RenderOitTransparentFuzzPass(renderInfo);
	}

	if (hasDynamicAlphaGeometry)
	{
		m_interpolationTransparentProgram->Bind();
		// Alpha walls are the exception to pixel gap correction. Only required for opaque walls.
		m_interpolationTransparentProgram->VertexGapClampUV(false);
		SetInterpolationUniforms(m_interpolationTransparentProgram, renderInfo);
		RenderAllAlpha(*m_worldDataManager, *m_interpolationTransparentProgram);

		if (m_worldDataManager->HasStyleToRender(RenderDataStyle::FogBarrier))
		{
			m_interpolationTransparentProgram->FogBarrier(true);
			SetPolygonOffsetFloodFill();
			m_worldDataManager->Render(RenderDataStyle::FogBarrier);
			EnablePolygonOffset(false);
		}
	}

	if (hasStaticAlphaGeometry)
	{
		m_staticTransparentProgram->Bind();
		m_staticTransparentProgram->VertexGapClampUV(false);
		SetStaticUniforms(m_staticTransparentProgram, renderInfo);
		RenderAllAlpha(m_geometryRenderer->StaticRenderer(), *m_staticTransparentProgram);

		if (m_geometryRenderer->StaticRenderer().HasStyleToRender(RenderDataStyle::FogBarrier))
		{
			m_staticTransparentProgram->FogBarrier(true);
			SetPolygonOffsetFloodFill();
			m_geometryRenderer->StaticRenderer().Render(RenderDataStyle::FogBarrier);
			EnablePolygonOffset(false);
		}
	}

	ResetBlendEquations();
	framebuffer->Bind();

	if (hasEntityAlphaData || hasEntityFuzzData)
	{
		m_entityRenderer->StartRenderOitCompositePass(renderInfo);
		RenderCompositeStyles(*m_entityRenderer);
	}

	SetBlendEquation(RenderDataStyle::Normal);

	if (hasDynamicAlphaGeometry)
	{
		m_interpolationCompositeProgram->Bind();
		m_interpolationCompositeProgram->VertexGapClampUV(false);
		SetInterpolationUniforms(m_interpolationCompositeProgram, renderInfo);
		RenderCompositeStyles(*m_worldDataManager);
		SetBlendEquation(RenderDataStyle::Normal);
	}

	if (hasStaticAlphaGeometry)
	{
		m_staticCompositeProgram->Bind();
		m_staticCompositeProgram->VertexGapClampUV(false);
		SetStaticUniforms(m_staticCompositeProgram, renderInfo);
		RenderCompositeStyles(m_geometryRenderer->StaticRenderer());
		SetBlendEquation(RenderDataStyle::Normal);
	}

	if (hasEntityFuzzData)
		m_entityRenderer->RenderOitFuzzRefractionPass(renderInfo, true);

	State().SetDepthMask(true);
}

void LegacyWorldRenderer::RenderCompositeStyles(IStyleRenderer& styleRenderer)
{
	styleRenderer.Render(RenderDataStyle::Translucent);

	if (styleRenderer.HasStyleToRender(RenderDataStyle::FogBarrier))
	{
		m_staticCompositeProgram->FogBarrier(true);
		SetPolygonOffsetFloodFill();
		styleRenderer.Render(RenderDataStyle::FogBarrier);
		EnablePolygonOffset(false);
	}

	if (styleRenderer.HasStyleToRender(RenderDataStyle::Add))
	{
		SetBlendEquation(RenderDataStyle::Add);
		styleRenderer.Render(RenderDataStyle::Add);
	}

	if (styleRenderer.HasStyleToRender(RenderDataStyle::ColorAdd))
	{
		SetBlendEquation(RenderDataStyle::ColorAdd);
		styleRenderer.Render(RenderDataStyle::ColorAdd);
	}
}

void LegacyWorldRenderer::SetBlendEquation(RenderDataStyle style)
{
	switch (style)
	{
	case RenderDataStyle::Add:
		State().SetCustomBlend(RBF_SrcAlpha, RBF_One, RBF_SrcAlpha, RBF_One);
		break;
	case RenderDataStyle::ColorAdd:
		State().SetCustomBlend(RBF_SrcColor, RBF_One, RBF_SrcColor, RBF_One);
		break;
	default:
		State().SetCustomBlend(RBF_SrcAlpha, RBF_OneMinusSrcAlpha, RBF_One, RBF_OneMinusSrcAlpha);
		break;
	}
}

void LegacyWorldRenderer::ResetBlendEquations()
{
	// GL.BlendEquation(FuncAdd), GL.Enable(Blend), GL.BlendFunc(SrcAlpha, OneMinusSrcAlpha)
	State().SetCustomBlend(RBF_SrcAlpha, RBF_OneMinusSrcAlpha, RBF_One, RBF_OneMinusSrcAlpha);
}

void LegacyWorldRenderer::RenderTwoSidedMiddleWalls(Shared::RenderInfo& renderInfo)
{
	m_interpolationProgram->Bind();
	SetInterpolationUniforms(m_interpolationProgram, renderInfo);
	m_interpolationProgram->VertexGapClampUV(m_pixelGapCorrection);
	m_worldDataManager->RenderTwoSidedMiddleWalls();

	if (Helion::World::WorldStatic::Sector3D)
		m_worldDataManager->RenderMiddle3D();

	if (m_renderStatic)
	{
		m_staticProgram->Bind();
		SetStaticUniforms(m_staticProgram, renderInfo);
		m_staticProgram->VertexGapClampUV(m_pixelGapCorrection);
		m_geometryRenderer->RenderStaticTwoSidedWalls();

		if (Helion::World::WorldStatic::Sector3D)
			m_geometryRenderer->RenderStaticMiddle3D();
	}
}

void LegacyWorldRenderer::ResetInterpolation(FLevelLocals* world)
{
	m_entityRenderer->ResetInterpolation(world);
	m_lastTicker = -1;
}

void LegacyWorldRenderer::SetOccludePosition(const Vec3D& position, double angleRadians, double pitchRadians, bool& occlude, Vec2D& occludeViewPos)
{
	// This is a hack until frustum culling exists.
	// Push the position back to stop occluding things that are straight up/down
	if (std::abs(pitchRadians) > ::Helion::Util::MathHelper::HalfPi / 2)
	{
		occlude = false;
		return;
	}

	occlude = true;
	Vec2D unit = Vec2D(std::cos(angleRadians + ::Helion::Util::MathHelper::Pi), std::sin(angleRadians + ::Helion::Util::MathHelper::Pi));
	occludeViewPos = Vec2D(position.X, position.Y) + (unit * 32);
}

// UZDoom (plan M5.2): the RestoreView hook of the portal driver (screen->mViewpoints->Bind(state, vpIndex) of
// classic RenderPortal): back to the outer view after a portal view; the programs take the viewpoint from the render info.
void LegacyWorldRenderer::RestorePortalView(const Geometry::Portals::PortalFrameView& view)
{
	FRenderState& state = State();
	if (view.RenderInfo != nullptr)
	{
		m_currentRenderInfo = view.RenderInfo;
		const IntRect& viewport = view.RenderInfo->Viewport;
		state.SetScissor(viewport.left, viewport.top, viewport.width, viewport.height);
	}
	s_windingFlipped = view.Clip != nullptr && view.Clip->WindingFlipped();
	state.SetCulling(view.Culling);
	state.EnableDepthTest(true);
	state.SetDepthMask(true);
	state.SetDepthFunc(DF_Less);
	state.SetColorMask(true);
}

void LegacyWorldRenderer::Clear(FLevelLocals* world, Shared::RenderInfo& renderInfo, bool renderTickChange)
{
	m_geometryRenderer->Clear(renderInfo.TickFraction, renderTickChange);
	m_primitiveRenderer->Clear();

	if (renderTickChange)
	{
		m_entityRenderer->Clear(world);
		m_modelRenderer->Clear();
		m_coronaRenderer->Clear();
		m_worldDataManager->Clear();
	}
}

void LegacyWorldRenderer::QueueFrame(FLevelLocals* world, Shared::RenderInfo& renderInfo)
{
	// The bucket is refilled every frame after the walk (the entity data lives a whole tick), so the emission order is not
	// the BSP order: the items are always sorted by their squared distance to the viewer.
	m_sortedRenderer->Clear();
	m_sortedRenderer->SetBspMode(false);
	m_entityRenderer->QueueSortedDraws(renderInfo);
	m_modelRenderer->QueueFrame(renderInfo);
	GatherDecals(world, renderInfo);
}

// HWWall::ProcessDecals runs for every solid wall the BSP walk emits. The decal lists hang off the sides; the sides that carry
// one are found through the decal thinkers (one per decal: STAT_DECAL for placed decals, STAT_AUTODECAL for impact decals) and
// kept when the viewer is on their front within the max distance.
void LegacyWorldRenderer::GatherDecals(FLevelLocals* world, Shared::RenderInfo& renderInfo)
{
	m_decalRenderer->Clear();
	if (m_sideDecalStamp.size() != world->sides.Size())
		m_sideDecalStamp.assign(world->sides.Size(), 0);
	m_decalStamp++;

	const Vec3F& view = renderInfo.Camera->PositionInterpolated;
	const double maxDistanceSquared = m_renderData.MaxDistanceSquared;

	for (int statnum : { STAT_DECAL, STAT_AUTODECAL })
	{
		TThinkerIterator<DBaseDecal> it(world, statnum);
		for (DBaseDecal* decal = it.Next(); decal != nullptr; decal = it.Next())
		{
			side_t* side = decal->Side;
			if (side == nullptr || side->linedef == nullptr)
				continue;

			int& stamp = m_sideDecalStamp[side->Index()];
			if (stamp == m_decalStamp)
				continue;
			stamp = m_decalStamp;

			line_t* line = side->linedef;
			const bool isFront = line->sidedef[0] == side;
			const vertex_t* v1 = isFront ? line->v1 : line->v2;
			const vertex_t* v2 = isFront ? line->v2 : line->v1;
			// The front of a side is on its right: skip it when the viewer is behind the line.
			const double cross = (v2->fX() - v1->fX()) * (view.Y - v1->fY()) - (v2->fY() - v1->fY()) * (view.X - v1->fX());
			if (cross > 0)
				continue;

			// Distance from the viewer to the line's bounding box.
			const double dx = std::max({ 0.0, std::min(v1->fX(), v2->fX()) - view.X, view.X - std::max(v1->fX(), v2->fX()) });
			const double dy = std::max({ 0.0, std::min(v1->fY(), v2->fY()) - view.Y, view.Y - std::max(v1->fY(), v2->fY()) });
			if (dx * dx + dy * dy > maxDistanceSquared)
				continue;

			m_geometryRenderer->RenderSideDecals(side, *m_decalRenderer, renderInfo);
		}
	}
}

void LegacyWorldRenderer::RenderPlayerSprites(Shared::RenderInfo& renderInfo, bool hudModelStep)
{
	m_hudSortedRenderer->Clear();
	m_weaponRenderer->Render(renderInfo, hudModelStep);
	// The 2D step draws into the post processing target over the whole scene (classic DrawEndScene2D: no depth test).
	m_hudSortedRenderer->Render(renderInfo, !hudModelStep);
	EndWorldPass();
}

void LegacyWorldRenderer::PopulatePrimitives(FLevelLocals* world)
{
	// Helion adds the player's bullet tracers (Player.Tracers); UZDoom has no tracer list, so nothing is added.
	(void)world;
}

void LegacyWorldRenderer::SetInterpolationUniforms(InterpolationShader* program, Shared::RenderInfo& renderInfo)
{
	program->Bind();
	program->BoundTexture(BindTextures::BoundTexture);
	program->SectorLightTexture(BindTextures::SectorLight);
	program->ColormapTexture(BindTextures::Colormap);
	program->SectorColormapTexture(BindTextures::SectorColormap);
	program->SectorFogTexture(BindTextures::SectorFog);
	program->SectorGlowTexture(BindTextures::SectorGlow);
	program->BrightmapTexture(BindTextures::BrightmapTexture);
	program->PlaneClipTexture(BindTextures::PlaneClipTexture);
	program->WallClipTexture(BindTextures::WallClipTexture);
	program->HasInvulnerability(renderInfo.Uniforms.DrawInvulnerability);
	program->Mvp(renderInfo.Uniforms.Mvp);
	program->MvpNoPitch(renderInfo.Uniforms.MvpNoPitch);
	program->TimeFrac(renderInfo.TickFraction);
	program->LightLevelMix(renderInfo.Uniforms.Mix);
	program->ExtraLight(renderInfo.Uniforms.ExtraLightOrColorMapIndex);
	program->DistanceOffset(renderInfo.Uniforms.DistanceOffset);
	program->ColorMix(renderInfo.Uniforms.ColorMix.Global);
	program->PaletteIndex((int)renderInfo.Uniforms.PaletteIndex);
	program->ColorMapIndex(renderInfo.Uniforms.ColorMapUniforms.GlobalIndex);
	program->LightMode(renderInfo.Uniforms.LightMode);
	program->GammaCorrection(renderInfo.Uniforms.GammaCorrection);
	program->UseBrightmaps(renderInfo.Uniforms.UseBrightmaps);
	program->UseSectorColor(renderInfo.Uniforms.SectorColor);
	program->UseSectorFog(renderInfo.Uniforms.SectorFogIndex);
	program->SetSpriteClipDownScaleAmount(renderInfo.Uniforms.DownScaleAmount);
	program->ScreenBounds(Vec2I(renderInfo.Viewport.width - 1, renderInfo.Viewport.height - 1));
	program->CheckPlaneClip(false);
	program->FogBarrier(false);
	if (renderInfo.Viewport.width > 0 && renderInfo.Viewpoint != nullptr)
		program->ViewpointUniforms(*renderInfo.Viewpoint);

	if (dynamic_cast<InterpolationCompositeShader*>(program) != nullptr)
	{
		program->AccumTexture(BindTextures::AccumTexture);
		program->AccumCountTexture(BindTextures::AccumCountTexture);
	}

	if (dynamic_cast<InterpolationTransparentShader*>(program) != nullptr || dynamic_cast<InterpolationCompositeShader*>(program) != nullptr)
		program->CheckPlaneClip(m_vanillaRender);
}

void LegacyWorldRenderer::SetStaticUniforms(StaticShader* program, Shared::RenderInfo& renderInfo)
{
	program->BoundTexture(BindTextures::BoundTexture);
	program->SectorLightTexture(BindTextures::SectorLight);
	program->ColormapTexture(BindTextures::Colormap);
	program->SectorColormapTexture(BindTextures::SectorColormap);
	program->SectorFogTexture(BindTextures::SectorFog);
	program->SectorGlowTexture(BindTextures::SectorGlow);
	program->BrightmapTexture(BindTextures::BrightmapTexture);
	program->PlaneClipTexture(BindTextures::PlaneClipTexture);
	program->WallClipTexture(BindTextures::WallClipTexture);
	program->HasInvulnerability(renderInfo.Uniforms.DrawInvulnerability);
	program->Mvp(renderInfo.Uniforms.Mvp);
	program->MvpNoPitch(renderInfo.Uniforms.MvpNoPitch);
	program->LightLevelMix(renderInfo.Uniforms.Mix);
	program->ExtraLight(renderInfo.Uniforms.ExtraLightOrColorMapIndex);
	program->DistanceOffset(renderInfo.Uniforms.DistanceOffset);
	program->ColorMix(renderInfo.Uniforms.ColorMix.Global);
	program->PaletteIndex((int)renderInfo.Uniforms.PaletteIndex);
	program->ColorMapIndex(renderInfo.Uniforms.ColorMapUniforms.GlobalIndex);
	program->LightMode(renderInfo.Uniforms.LightMode);
	program->GammaCorrection(renderInfo.Uniforms.GammaCorrection);
	program->UseBrightmaps(renderInfo.Uniforms.UseBrightmaps);
	program->UseSectorColor(renderInfo.Uniforms.SectorColor);
	program->UseSectorFog(renderInfo.Uniforms.SectorFogIndex);
	program->SetSpriteClipDownScaleAmount(renderInfo.Uniforms.DownScaleAmount);
	program->ScreenBounds(Vec2I(renderInfo.Viewport.width - 1, renderInfo.Viewport.height - 1));
	program->CheckPlaneClip(false);
	program->FogBarrier(false);
	if (renderInfo.Viewpoint != nullptr)
		program->ViewpointUniforms(*renderInfo.Viewpoint);

	if (dynamic_cast<StaticCompositeShader*>(program) != nullptr)
	{
		program->AccumTexture(BindTextures::AccumTexture);
		program->AccumCountTexture(BindTextures::AccumCountTexture);
	}

	if (dynamic_cast<StaticCompositeShader*>(program) != nullptr || dynamic_cast<StaticTransparentShader*>(program) != nullptr)
		program->CheckPlaneClip(m_vanillaRender);
}

void LegacyWorldRenderer::ReleaseUnmanagedResources()
{
	if (m_released)
		return;
	m_released = true;
	Data::RenderWorldData::MaterialDraw = nullptr;
	Lights::ViewLights::SetCurrent(nullptr);

	// Helion also disposes the programs: they are owned by HelionProgramRegistry here.
	delete m_geometryRenderer;
	m_geometryRenderer = nullptr;
	delete m_worldDataManager;
	m_worldDataManager = nullptr;
	delete m_primitiveRenderer;
	m_primitiveRenderer = nullptr;
	delete m_entityRenderer;
	m_entityRenderer = nullptr;
	delete m_weaponRenderer;
	m_weaponRenderer = nullptr;
	delete m_modelRenderer;
	m_modelRenderer = nullptr;
	delete m_decalRenderer;
	m_decalRenderer = nullptr;
	delete m_coronaRenderer;
	m_coronaRenderer = nullptr;
	delete m_sortedRenderer;
	m_sortedRenderer = nullptr;
	delete m_hudSortedRenderer;
	m_hudSortedRenderer = nullptr;
	delete m_portalDriver;
	m_portalDriver = nullptr;
	delete m_oitFrameBuffer;
	m_oitFrameBuffer = nullptr;
	delete m_planeClipFrameBuffer;
	m_planeClipFrameBuffer = nullptr;
	delete m_wallClipFrameBuffer;
	m_wallClipFrameBuffer = nullptr;
}

}
