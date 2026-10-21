/*
** LegacyWorldRenderer.Portals.cpp
**
** UZDoom-only partial of LegacyWorldRenderer: the portal views (plan M5.2, D26).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** PerformRender starts a portal frame of the driver before its gather and ends it after its opaque geometry
** (FPortalSceneState::StartFrame / EndFrame). Every collected portal draws its stencil and calls the RenderView
** hook, which Renderer binds to its RenderPortalView: that sets the portal viewpoint up and comes back here
** through RenderPortalView, which runs PerformRender for the view with the PortalClip and restores the state of
** the outer view afterwards. A view that walks the BSP in the frame hands its visible segs to the driver
** (GeometryRenderer::SetPortalDriver); otherwise CollectPortals offers the portal candidates of the level that
** face the viewer.
*/

#include <vector>
#include "OpenGL/Renderers/Legacy/World/LegacyWorldRenderer.h"
#include "OpenGL/Renderers/Legacy/World/BspBox.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalDriver.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/FloodFill/FloodFillRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityModelRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Sky/LegacySkyRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Sky/ISkyComponent.h"
#include "Common/Shared/Frustum.h"
#include "World/Geometry/Sectors/RenderSector.h"
#include "g_levellocals.h"
#include "r_utility.h"
#include "v_video.h"
#include "hwrenderer/data/hw_renderstate.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

using namespace Helion::Geometry;
using Geometry::Portals::PortalClip;
using Geometry::Portals::PortalDriver;
using Geometry::Portals::PortalFrameView;
using Geometry::Portals::HorizonDraw;
using Geometry::Portals::HorizonVertex;

void LegacyWorldRenderer::BeginScene(FLevelLocals* world)
{
	m_portalDriver->SetLevel(world);
	m_portalDriver->BeginScene();
}

void LegacyWorldRenderer::RenderPortalView(FLevelLocals* world, Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer,
	const PortalClip& clip)
{
	// The state of the outer view that PerformRender of the portal view overwrites and the rest of the outer pass reads.
	const PortalClip* savedClip = m_portalClip;
	Shared::RenderInfo* savedRenderInfo = m_currentRenderInfo;
	bool savedRenderStatic = m_renderStatic;
	bool savedLastRenderStatic = m_lastRenderStatic;
	bool savedLastUseBsp = m_lastUseBsp;
	bool savedChaseCam = m_chaseCam;
	bool savedOcclude = m_occlude;
	Vec2D savedOccludeViewPos = m_occludeViewPos;
	AActor* savedViewerEntity = m_viewerEntity;
	::Helion::World::Geometry::Sectors::TransferHeightView savedTransferHeightsView = m_lastTransferHeightsView;
	bool savedEntityInMirror = m_entityRenderer->InMirror;
	bool savedModelInMirror = m_modelRenderer->InMirror;

	m_portalClip = &clip;
	m_entityRenderer->InMirror = clip.InMirror;
	m_modelRenderer->InMirror = clip.WindingFlipped();

	// The clustered lights of the portal view (plan D25) go to the next recursion depth; the outer view's stay valid.
	m_viewLightsDepth++;
	PerformRender(world, renderInfo, framebuffer);
	m_viewLightsDepth--;
	Lights::ViewLights::SetCurrent(m_viewLights[m_viewLightsDepth].get());
	m_lightAdditiveSurfaces = world->lightadditivesurfaces;

	m_portalClip = savedClip;
	m_currentRenderInfo = savedRenderInfo;
	m_renderStatic = savedRenderStatic;
	m_lastRenderStatic = savedLastRenderStatic;
	m_lastUseBsp = savedLastUseBsp;
	m_chaseCam = savedChaseCam;
	m_occlude = savedOcclude;
	m_occludeViewPos = savedOccludeViewPos;
	m_viewerEntity = savedViewerEntity;
	m_lastTransferHeightsView = savedTransferHeightsView;
	m_entityRenderer->InMirror = savedEntityInMirror;
	m_modelRenderer->InMirror = savedModelInMirror;
	// The gathered data is the portal view's now.
	m_lastTicker = -1;
}

void LegacyWorldRenderer::BuildPortalCandidates(FLevelLocals* world)
{
	m_portalSegs.clear();
	m_portalSubsectors.clear();

	for (unsigned i = 0; i < world->segs.Size(); i++)
	{
		seg_t* seg = &world->segs[i];
		if (seg->sidedef == nullptr || seg->linedef == nullptr || seg->frontsector == nullptr)
			continue;
		if (PortalDriver::LineHasPortal(seg->linedef) || PortalDriver::SectorHasPortalPlanes(seg->frontsector))
			m_portalSegs.push_back(seg);
	}

	for (unsigned i = 0; i < world->subsectors.Size(); i++)
	{
		subsector_t* sub = &world->subsectors[i];
		if (sub->sector != nullptr && (sub->sector->GetPortalGroup(sector_t::floor) != nullptr || sub->sector->GetPortalGroup(sector_t::ceiling) != nullptr))
			m_portalSubsectors.push_back(sub);
	}
}

void LegacyWorldRenderer::CollectPortals(FLevelLocals* world, Shared::RenderInfo& renderInfo)
{
	if (m_portalSegs.empty() && m_portalSubsectors.empty())
		return;

	const Vec3F& camera = renderInfo.Camera->PositionInterpolated;
	DVector3 viewPos(camera.X, camera.Y, camera.Z);
	double maxDistance = renderInfo.Uniforms.MaxDistance;
	double maxDistanceSquared = maxDistance > 0 ? maxDistance * maxDistance : 0;

	Common::Shared::FrustumPlanes frustum;
	Common::Shared::Frustum::SetFrustumPlanes(renderInfo.Uniforms.MvpNoPitch, frustum);

	auto boxVisible = [&](const Box2D& box)
	{
		if (BspBox::Contains(box, Vec2D(viewPos.X, viewPos.Y)))
			return true;
		if (m_occlude && !frustum.BoxInFront(box))
			return false;
		if (maxDistanceSquared > 0)
		{
			double dx = std::max({ 0.0, box.Min.X - viewPos.X, viewPos.X - box.Max.X });
			double dy = std::max({ 0.0, box.Min.Y - viewPos.Y, viewPos.Y - box.Max.Y });
			if (dx * dx + dy * dy > maxDistanceSquared)
				return false;
		}
		return true;
	};

	// The static mode draws the whole map: only the candidates the BSP walk of the main view reached have a view (the
	// walk hides the portals behind walls like the walk of the BSP mode, which collects them itself).
	const bool fromWalk = m_renderStatic && m_portalClip == nullptr && renderInfo.MainView && m_bspHeuristicsImpl.Valid();
	const std::vector<seg_t*>& segs = fromWalk ? m_bspHeuristicsImpl.SeenPortalSegs() : m_portalSegs;
	const std::vector<subsector_t*>& subsectors = fromWalk ? m_bspHeuristicsImpl.SeenPortalSubsectors() : m_portalSubsectors;

	for (seg_t* seg : segs)
	{
		// classic AddLine: the back of a seg is never seen.
		double startX = seg->v1->fX(), startY = seg->v1->fY();
		double dx = seg->v2->fX() - startX, dy = seg->v2->fY() - startY;
		if ((dx * (viewPos.Y - startY)) - (dy * (viewPos.X - startX)) >= 0)
			continue;

		if (m_portalClip != nullptr)
		{
			if (seg->Subsector != nullptr && !m_portalClip->IsMapSectionVisible(seg->Subsector->mapsection))
				continue;
			if (m_portalClip->ClipPortal != nullptr && m_portalClip->ClipPortal->ClipSeg(seg, viewPos) == Geometry::Portals::PClipInFront)
				continue;
		}

		Box2D box(Vec2D(std::min(seg->v1->fX(), seg->v2->fX()), std::min(seg->v1->fY(), seg->v2->fY())),
			Vec2D(std::max(seg->v1->fX(), seg->v2->fX()), std::max(seg->v1->fY(), seg->v2->fY())));
		if (!boxVisible(box))
			continue;

		m_portalDriver->ProcessSeg(seg, seg->frontsector, seg->backsector, seg->Subsector);
	}

	for (subsector_t* sub : subsectors)
	{
		if (m_portalClip != nullptr && !m_portalClip->IsMapSectionVisible(sub->mapsection))
			continue;
		if (!boxVisible(BspBox::FromBoundingBox(sub->bbox)))
			continue;
		m_portalDriver->ProcessSubsector(sub, sub->sector);
	}
}

void LegacyWorldRenderer::DrawHorizon(const HorizonDraw& draw)
{
	const Geometry::Portals::HorizonPlane& plane = draw.Info->Plane;
	if (plane.Sector == nullptr || m_currentRenderInfo == nullptr)
		return;

	using ::Helion::World::Geometry::Sectors::RenderPlane;
	using ::Helion::World::Geometry::Sectors::SectorPlaneFace;
	RenderPlane renderPlane = RenderPlane::From(plane.Sector, plane.PlaneIndex == sector_t::floor ? SectorPlaneFace::Floor : SectorPlaneFace::Ceiling);

	// The strips of four vertices and the skirt strip as triangles. The flood fill program shows the plane where the view ray
	// meets it, so the skirt reaches the true horizon.
	std::vector<Vec3F> triangles;
	auto addStrip = [&](const std::vector<HorizonVertex>& strip, size_t first, size_t count)
	{
		for (size_t i = 0; i + 2 < count; i++)
		{
			const HorizonVertex& a = strip[first + i];
			const HorizonVertex& b = strip[first + i + 1];
			const HorizonVertex& c = strip[first + i + 2];
			triangles.push_back(Vec3F(a.X, a.Y, a.Z));
			triangles.push_back(Vec3F(b.X, b.Y, b.Z));
			triangles.push_back(Vec3F(c.X, c.Y, c.Z));
		}
	};
	if (draw.Strips != nullptr)
	{
		for (size_t i = 0; i + 3 < draw.Strips->size(); i += 4)
			addStrip(*draw.Strips, i, 4);
	}
	if (draw.Skirt != nullptr)
		addStrip(*draw.Skirt, 0, draw.Skirt->size());

	screen->RenderState()->SetCulling(Cull_None);
	m_geometryRenderer->Portals().GetStaticFloodFillRenderer().RenderHorizon(*m_currentRenderInfo, renderPlane, triangles);
}

void LegacyWorldRenderer::DrawSectorSky(sector_t* sector, int plane)
{
	// HWSkyPortal::DrawContents: the level sky of the plane over the whole stencil area.
	if (m_currentRenderInfo == nullptr)
		return;

	Sky::ISkyComponent* sky = nullptr;
	if (!m_geometryRenderer->SkyRenderer().GetOrCreateSectorSky(sector, plane, sky) || sky == nullptr)
		return;

	FRenderState& state = *screen->RenderState();
	state.EnableDepthTest(false);
	sky->RenderSky(*m_currentRenderInfo);
	state.EnableDepthTest(true);
}

}
