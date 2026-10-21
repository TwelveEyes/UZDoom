/*
** LegacyWorldRenderer.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.LegacyWorldRenderer (+ .Bsp, .BspHeuristics partials).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Mapping: IConfig is the CVars of HelionCVars.h (config.Render.* -> gl_*), IWorld is FLevelLocals*, Entity is
** AActor*, ArchiveCollection is dropped (the texture manager reads UZDoom's texture manager), RenderProfiler
** is not ported (its Start/Stop calls are omitted). The programs are not owned: Helion constructs them in
** the field initialisers, here they come from HelionProgramRegistry (compiled at startup, plan D22).
**
** UZDoom-only: the entity milestone's subsystems Helion does not have (EntityModelRenderer, WeaponRenderer, DecalRenderer,
** CoronaRenderer, SortedTranslucentRenderer, plan M4 / D10) are owned here next to Helion's EntityRenderer; the decals are
** gathered per frame from the sides of the level's decal thinkers (GeometryRenderer::RenderSideDecals), the weapon is drawn by
** RenderPlayerSprites from the Entrypoint (HUD model step / 2D step, hw_entrypoint's order).
**
** UZDoom-only: ValidateSignatures() (plan D14), the Island-less RenderSectors, BspHeuristics (the heuristic is
** measured synchronously by BspHeuristics instead of a worker thread, plan D15), the visit stamps of sectors
** and sides (Helion keeps them in Sector.CheckCount / Side.BlockmapCount).
*/

#pragma once

#include <functional>
#include <optional>
#include <vector>
#include "OpenGL/Renderers/WorldRenderer.h"
#include "OpenGL/Renderers/IStyleRenderer.h"
#include "OpenGL/Renderers/Legacy/World/BspHeuristics.h"
#include "OpenGL/Renderers/Legacy/World/DitherTransparency.h"
#include "OpenGL/Renderers/Legacy/World/RenderBlockMapData.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationShader.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationCompositeShader.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationPlaneClipAlphaShader.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationPlaneClipShaderMrt.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationTransparentShader.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationWallClipAlphaShader.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationWallClipShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticCompositeShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticPlaneClipAlphaShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticPlaneClipShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticPlaneClipShaderMrt.h"
#include "OpenGL/Renderers/Legacy/World/StaticShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticTransparentShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticWallClipAlphaShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticWallClipShader.h"
#include "Common/Shared/Frustum.h"
#include "Common/Shared/World/ViewClipping/ViewClipper.h"
#include "World/Geometry/Sectors/TransferHeightsView.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderDataStyle.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ViewLights.h"

struct seg_t;
struct subsector_t;
struct FPolyObj;

namespace Helion::Render::OpenGL::Texture::Legacy
{
class LegacyGLTextureManager;
class GLLegacyTexture;
}

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

namespace Geometry
{
class GeometryRenderer;
class DecalRenderer;
namespace Portals
{
class PortalDriver;
struct PortalClip;
struct PortalFrameView;
struct HorizonDraw;
}
}

namespace Data
{
class RenderWorldDataManager;
class SortedTranslucentRenderer;
}

namespace Entities
{
class EntityRenderer;
class EntityModelRenderer;
class WeaponRenderer;
class CoronaRenderer;
}

namespace Primitives
{
class PrimitiveWorldRenderer;
}

}

namespace Helion::Render::OpenGL::Framebuffer
{
class OitFrameBuffer;
class PlaneClipFrameBuffer;
}

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

class LegacyWorldRenderer : public WorldRenderer
{
public:
	explicit LegacyWorldRenderer(Texture::Legacy::LegacyGLTextureManager& textureManager);
	~LegacyWorldRenderer() override;

	void Dispose() override;

	void UpdateToNewWorld(FLevelLocals* world) override;

	void ResetInterpolation(FLevelLocals* world) override;

	static void SetBlendEquation(Data::RenderDataStyle style);
	static void ResetBlendEquations();

	static void SetOccludePosition(const ::Helion::Geometry::Vec3D& position, double angleRadians, double pitchRadians,
		bool& occlude, ::Helion::Geometry::Vec2D& occludeViewPos);

	// UZDoom (hw_entrypoint's EndDrawScene / DrawEndScene2D): the player sprites of the last rendered view. hudModelStep: the
	// weapon models over the scene before the post processing; otherwise the 2D sprites into the current render target (the
	// callback of PostProcessScene). The viewport / projection are the weapon renderer's.
	void RenderPlayerSprites(Shared::RenderInfo& renderInfo, bool hudModelStep);

	// UZDoom (plan M5.2): the portal driver of the world passes (Renderer binds its RenderView hook).
	Geometry::Portals::PortalDriver& Portals() { return *m_portalDriver; }
	// Start of a Helion_RenderViewpoint scene (FPortalSceneState::BeginScene).
	void BeginScene(FLevelLocals* world);
	// The render info of the view that is being drawn (null outside of a world pass).
	const Shared::RenderInfo* CurrentRenderInfo() const { return m_currentRenderInfo; }
	// The world pass of a portal view (the RenderView hook of the portal driver through Renderer). The outer view's state is
	// restored when it returns.
	void RenderPortalView(FLevelLocals* world, Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer,
		const Geometry::Portals::PortalClip& clip);

protected:
	void PerformRender(FLevelLocals* world, Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer) override;

private:
	static constexpr int EntityRenderIndexMax = 100;
	// Helion.Util.Configs.Components.ConfigRender.Adaptive.SwitchCount default (no CVar, plan D28).
	static constexpr int AdaptiveSwitchCount = 3;

	Texture::Legacy::LegacyGLTextureManager& m_textureManager;

	// Not owned (HelionProgramRegistry).
	InterpolationShader* m_interpolationProgram;
	InterpolationTransparentShader* m_interpolationTransparentProgram;
	InterpolationCompositeShader* m_interpolationCompositeProgram;
	InterpolationPlaneClipAlphaShader* m_interpolationPlaneClipAlphaProgram;
	InterpolationPlaneClipShaderMrt* m_interpolationPlaneClipMrtProgram;
	InterpolationWallClipShader* m_interpolationWallClipShader;
	InterpolationWallClipAlphaShader* m_interpolationWallClipAlphaProgram;
	StaticShader* m_staticProgram;
	StaticPlaneClipShader* m_staticPlaneClipProgram;
	StaticPlaneClipAlphaShader* m_staticPlaneClipAlphaProgram;
	StaticPlaneClipShaderMrt* m_staticPlaneClipMrtProgram;
	StaticWallClipShader* m_staticWallClipProgram;
	StaticWallClipAlphaShader* m_staticWallClipAlphaProgram;
	StaticTransparentShader* m_staticTransparentProgram;
	StaticCompositeShader* m_staticCompositeProgram;

	// Owned.
	Data::RenderWorldDataManager* m_worldDataManager = nullptr;
	Geometry::GeometryRenderer* m_geometryRenderer = nullptr;
	Entities::EntityRenderer* m_entityRenderer = nullptr;
	Primitives::PrimitiveWorldRenderer* m_primitiveRenderer = nullptr;
	Framebuffer::OitFrameBuffer* m_oitFrameBuffer = nullptr;
	Framebuffer::PlaneClipFrameBuffer* m_planeClipFrameBuffer = nullptr;
	Framebuffer::PlaneClipFrameBuffer* m_wallClipFrameBuffer = nullptr;
	// UZDoom-only (see the file comment). m_hudSortedRenderer: the sorted draws of the player sprites.
	Entities::EntityModelRenderer* m_modelRenderer = nullptr;
	Data::SortedTranslucentRenderer* m_sortedRenderer = nullptr;
	Data::SortedTranslucentRenderer* m_hudSortedRenderer = nullptr;
	Geometry::DecalRenderer* m_decalRenderer = nullptr;
	Entities::CoronaRenderer* m_coronaRenderer = nullptr;
	Entities::WeaponRenderer* m_weaponRenderer = nullptr;
	// UZDoom (plan M5.2): portals. m_portalClip is the clip of the portal view that is being rendered (null for the main view).
	// m_portalSegs / m_portalSubsectors: the segs / subsectors that can produce portal surfaces (PortalDriver::LineHasPortal,
	// SectorHasPortalPlanes, sector stack portal groups), for the frames that do not walk the BSP.
	Geometry::Portals::PortalDriver* m_portalDriver = nullptr;
	const Geometry::Portals::PortalClip* m_portalClip = nullptr;
	std::vector<seg_t*> m_portalSegs;
	std::vector<subsector_t*> m_portalSubsectors;
	std::vector<int> m_sideDecalStamp;
	int m_decalStamp = 0;
	// UZDoom (plan D25): the clustered dynamic lights of the view being drawn, one per portal recursion depth (the views of
	// an outer pass keep theirs while a portal view is drawn). m_lightAdditiveSurfaces: FLevelLocals::lightadditivesurfaces.
	std::vector<std::unique_ptr<Lights::ViewLights>> m_viewLights;
	int m_viewLightsDepth = 0;
	bool m_lightAdditiveSurfaces = false;

	Shared::RenderInfo m_downSizedRenderInfo;
	bool m_vanillaRender;
	::Helion::Geometry::Vec2D m_occludeViewPos = ::Helion::Geometry::Vec2D(0, 0);
	bool m_occlude = false;
	bool m_renderStatic = false;
	bool m_lastRenderStatic = false;
	bool m_pixelGapCorrection = false;
	bool m_downscaleVanillaBuffer = false;
	bool m_postProcessingEffects = true;
	bool m_lastUseBsp = false;
	bool m_spritesCached = false;
	bool m_chaseCam = false;
	int m_lastTicker = -1;
	AActor* m_viewerEntity = nullptr;
	FLevelLocals* m_previousWorld = nullptr;
	RenderBlockMapData m_renderData;
	::Helion::World::Geometry::Sectors::TransferHeightView m_lastTransferHeightsView = ::Helion::World::Geometry::Sectors::TransferHeightView::Middle;

	// UZDoom-only: Sector.CheckCount / Side.BlockmapCount of Helion, indexed by sector / side index.
	std::vector<int> m_sectorCheckCount;
	std::vector<int> m_sideCheckCount;
	std::vector<::Helion::Geometry::Box2D> m_sectorBounds;
	Shared::RenderInfo* m_currentRenderInfo = nullptr;
	int m_lastBspHeuristicTick = -1;
	bool m_released = false;

	// BSP traversal (LegacyWorldRenderer.Bsp.cs)
	Shared::World::ViewClipping::ViewClipper m_viewClipperPrev;
	Shared::World::ViewClipping::ViewClipper m_viewClipper;
	Common::Shared::FrustumPlanes m_frustumPlanes;
	// UZDoom (plan D27): the view is out of bounds (FRenderViewpoint::bDoOob) / clips with the radar clipper, centred on OffPos.
	Shared::World::ViewClipping::ViewClipper m_radarClipper;
	bool m_outOfBounds = false;
	bool m_radar = false;
	// UZDoom: r_dithertransparency (the dither flags of the main view's walk).
	DitherTransparency m_dither;

	// BSP heuristics (LegacyWorldRenderer.BspHeuristics.cs)
	BspHeuristics m_bspHeuristicsImpl;
	IBspHeuristics* m_bspHeuristics = nullptr;
	double m_smoothedBspTimeUs = 0;
	std::vector<double> m_bspTimeWindow;
	int m_bspTimeWindowSize = 32;
	int m_aboveThresholdCount = 0;
	int m_belowThresholdCount = 0;
	int m_lastProcessedId = 0;
	int m_lastHeuristicTick = -1000;

	void ReleaseUnmanagedResources();

	bool NeedsRenderTickChange(FLevelLocals* world, ::Helion::World::Geometry::Sectors::TransferHeightView view) const;
	void IterateBlockmap(FLevelLocals* world);
	void SetupRenderData(FLevelLocals* world, Shared::RenderInfo& renderInfo);
	static bool BlockInView(int x, int y, int dimension, double originX, double originY, const ::Helion::Geometry::Vec2D& viewPos, const ::Helion::Geometry::Vec2D& viewDirection);
	void RenderSectors(FLevelLocals* world, int blockIndex);
	void RenderSides(FLevelLocals* world, int blockIndex);
	void RenderEntity(FLevelLocals* world, AActor* entity, int renderIndex);
	void RenderFloodFill(Shared::RenderInfo& renderInfo);
	void EndWorldPass();
	void WriteSpriteClipBuffers(Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer);
	void SetupClipBuffers(Framebuffer::GLFramebuffer* framebuffer, ::Helion::Geometry::Dimension dimension, bool downscaleChanged);
	void WritePlaneClipData(Framebuffer::PlaneClipFrameBuffer* planeClipFrameBuffer, Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer, bool walls);
	void RenderTransparent(Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer);
	void RenderCompositeStyles(IStyleRenderer& styleRenderer);
	// UZDoom (plan D25): IStyleRenderer::RenderAllAlpha for the OIT transparent pass of the world. Additive surfaces get
	// no dynamic lights unless the level has lightadditivesurfaces (HWWall / HWFlat::SetupLights).
	void RenderAllAlpha(IStyleRenderer& styleRenderer, RenderProgramBase& program);
	void BuildViewLights(FLevelLocals* world, const Shared::RenderInfo& renderInfo);
	void RenderTwoSidedMiddleWalls(Shared::RenderInfo& renderInfo);
	void Clear(FLevelLocals* world, Shared::RenderInfo& renderInfo, bool renderTickChange);
	void PopulatePrimitives(FLevelLocals* world);
	void SetInterpolationUniforms(InterpolationShader* program, Shared::RenderInfo& renderInfo);
	void SetStaticUniforms(StaticShader* program, Shared::RenderInfo& renderInfo);
	// UZDoom (LegacyWorldRenderer.Materials.cpp): draw() with the material program of a non-array texture; false = not a
	// material texture (or not the opaque pass), the caller draws with the array texture.
	bool DrawMaterialGeometry(Texture::Legacy::GLLegacyTexture* texture, const std::function<void()>& draw);
	void OnDynamicDraw();
	void GatherWorld(FLevelLocals* world, Shared::RenderInfo& renderInfo);
	void RenderOpaque(Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer, bool vanilla);
	void BuildPortalCandidates(FLevelLocals* world);
	void CollectPortals(FLevelLocals* world, Shared::RenderInfo& renderInfo);
	void RestorePortalView(const Geometry::Portals::PortalFrameView& view);
	void DrawHorizon(const Geometry::Portals::HorizonDraw& draw);
	void DrawSectorSky(sector_t* sector, int plane);

	// UZDoom-only (LegacyWorldRenderer.Validate.cpp): plan D14, compares the signatures of every sector / side with
	// the cached copy and marks mismatches dirty (the same path as the events), once per game tic.
	void ValidateSignatures(FLevelLocals* world);
	int m_lastValidatedTick = -1;

	// UZDoom-only: the per frame part of the entity milestone's gather: clears the sorted bucket, queues the sorted entity draws,
	// the models at the frame's interpolation and the decals of the sides in view.
	void QueueFrame(FLevelLocals* world, Shared::RenderInfo& renderInfo);
	void GatherDecals(FLevelLocals* world, Shared::RenderInfo& renderInfo);

	// Bsp
	void TraverseBsp(FLevelLocals* world, Shared::RenderInfo& renderInfo);
	void RecursivelyRenderBsp(void* node, const ::Helion::Geometry::Vec3D& position, const ::Helion::Geometry::Vec2D& pos2D, const ::Helion::Geometry::Vec2D& prevPos2D, FLevelLocals* world);
	void RenderBspSubsector(subsector_t* subsector, const ::Helion::Geometry::Vec3D& position, const ::Helion::Geometry::Vec2D& pos2D, const ::Helion::Geometry::Vec2D& prevPos2D, FLevelLocals* world);
	bool ShouldRenderBox(const ::Helion::Geometry::Box2D& box, const ::Helion::Geometry::Vec2D& pos2D, const ::Helion::Geometry::Vec2D& prevPos2D);
	bool RadarSubsector(subsector_t* subsector);
	void AddRadarClipRange(seg_t* seg, uint32_t Padding);
	static sector_t* PolyobjectSideSector(FLevelLocals* world, side_t* side, FPolyObj* poly);

	// BspHeuristics
	bool UseBspBasedOnHeuristic(FLevelLocals* world, Shared::RenderInfo& renderInfo);
	double AddBspTimeSample(double time);
};

}
