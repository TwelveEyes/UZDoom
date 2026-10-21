/*
** PortalDriver.h
**
** UZDoom-only (plan M5.2). The Helion-shaped replacement of the portal part of the classic scene renderer
** (FPortalSceneState + classic RenderPortal + HWWall::PutPortal / SkyPlane / SkyLine / DoHorizon, the portal branches
** of hw_walls.cpp / hw_sky.cpp). One driver lives in the LegacyWorldRenderer.
**
**---------------------------------------------------------------------------
**
** Copyright 2004-2018 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Lifecycle of a world pass (LegacyWorldRenderer::PerformRender, see the integration notes of the portals ticket):
**   BeginScene()                         once per Helion_RenderViewpoint (classic scene start: portalState.BeginScene())
**   StartFrame(view)                     at the start of every PerformRender, nested or not (FPortalSceneState::StartFrame)
**   ProcessSeg / ProcessSectorPlane ...  for every visible seg while the world is gathered (HWWall::Process portal branches)
**   RenderFirstSkyPortal(recursion)      optional, before the opaque geometry (sky portals without stencil)
**   EndFrame()                           after the opaque geometry and the level sky, before the transparent pass
**                                        (renders every collected portal: stencil, hooks.RenderView, stencil removal)
**
** The portal views are drawn by Hooks::RenderView, which the integrator binds to LegacyWorldRenderer::PerformRender for the
** viewpoint of the portal and the PortalClip that describes the clip plane, mirroring, visible map sections and BSP clip.
**
*/

#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <vector>
#include "Portal.h"
#include "PortalClip.h"
#include "OpenGL/Buffer/VertexBufferObject.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalStencilVertex.h"
#include "OpenGL/VertexPipeline.h"
#include "OpenGL/Renderers/Legacy/World/Data/SortedTranslucentRenderer.h"

struct FRenderViewpoint;
struct FLevelLocals;
struct side_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

class PortalStencilProgram;

// One vertex of the horizon plane geometry, in map coordinates (x, y, height) with the flat texture coordinates.
struct HorizonVertex
{
	float X, Y, Z;
	float U, V;
};

// What the hook that draws a horizon plane receives (HWHorizonPortal::DrawContents). Strips holds consecutive triangle strips
// of four vertices, Skirt one triangle strip of ten vertices closing the gap to the true horizon. The hook sets the
// material of Plane.Texture (a flat, brightmap enabled), the colour and fog of LightLevel / Colormap (texture->isFullbright()
// draws full bright without colour), the object colour SpecialColor, the flat texture matrix of Plane (offset, scale,
// angle, hw_SetPlaneTextureRotation), STYLE_Source with alpha function >= 0, and draws the strips with the depth state
// that is already set up. The hook has to leave the render state like it found it.
struct HorizonDraw
{
	const HorizonInfo* Info = nullptr;
	const std::vector<HorizonVertex>* Strips = nullptr;
	const std::vector<HorizonVertex>* Skirt = nullptr;
	const FRenderViewpoint* Viewpoint = nullptr;
};

struct PortalHooks
{
	// Renders the world from the portal view (required): LegacyWorldRenderer::PerformRender with the viewpoint and the
	// clip. Must start with PortalDriver::StartFrame and end with PortalDriver::EndFrame for the nested portals.
	std::function<void(const FRenderViewpoint& viewpoint, const PortalClip& clip)> RenderView;
	// Rebinds the viewpoint uniforms, vertex streams and culling of the outer view after RenderView returned
	// (screen->mViewpoints->Bind(state, vpIndex) of classic RenderPortal).
	std::function<void(const PortalFrameView& view)> RestoreView;
	// HWHorizonPortal::DrawContents drawing.
	std::function<void(const HorizonDraw& draw)> DrawHorizon;
	// The plain level sky of a sector plane over the whole stencil area (HWSkyInfo::init + HWSkyPortal::DrawContents of
	// HWEEHorizonPortal): LegacySkyRenderer draws the sky of the sector without geometry.
	std::function<void(sector_t* sector, int plane, const FRenderViewpoint& viewpoint)> DrawSectorSky;
	// classic ProcessActorsInPortal: adds the actors that stand in the destination group of a line portal to the world
	// pass of the portal view.
	std::function<void(FLinePortalSpan* destination)> ProcessActorsInPortal;
	// gl_mirror_envmap: the reflective layer over a mirror (RENDERWALL_MIRRORSURFACE), drawn with the translucent sorted
	// bucket.
	std::function<void(const PortalSurface& surface, Shared::RenderInfo& renderInfo)> DrawMirrorSurface;
	// A translucent portal plane (alpha < 1 for sector portals, 1 - reflect for plane mirrors): draws the flat of the
	// plane with the alpha, registered in the sorted bucket by AddTranslucentPlane.
	std::function<void(sector_t* sector, int plane, float alpha, Shared::RenderInfo& renderInfo)> DrawTranslucentPlane;
};

class PortalDriver
{
public:
	explicit PortalDriver(Data::SortedTranslucentRenderer* sortedTranslucent);
	~PortalDriver();

	PortalDriver(const PortalDriver&) = delete;
	PortalDriver& operator=(const PortalDriver&) = delete;

	PortalHooks Hooks;
	// The driver of the world renderer (one per renderer, set by the constructor); null while there is none.
	static PortalDriver* Instance() { return s_instance; }

	PortalSceneState& State() { return m_state; }
	const PortalSceneState& State() const { return m_state; }

	// The level that is drawn: the driver needs the BSP, sections and portal tables of it. Set before the first StartFrame
	// of a world pass.
	void SetLevel(FLevelLocals* level) { m_level = level; }

	// Forgets the shared horizon / plane mirror instances (FPortalSceneState::BeginScene).
	void BeginScene();

	// FPortalSceneState::StartFrame. view is copied (the viewpoint of the view too); the matrix of view.Mvp is the stencil MVP of
	// the view. At render depth 0 the sky box state and the stack exclusion are reset.
	void StartFrame(const PortalFrameView& view);
	// FPortalSceneState::EndFrame: renders and deletes the portals that were collected since the matching StartFrame. Returns true if
	// a portal view was rendered. The portal views reuse the gather state of the world renderer: a view that rendered portals has to
	// gather its own geometry again before it draws anything that depends on it (see the integration notes).
	bool EndFrame();
	// True when the current view collected a portal that EndFrame will draw.
	bool HasPortals() const;
	// While set, ProcessSeg / ProcessSubsector only compute their results (a seg that a portal replaces) and collect no portals: for the
	// second gather of a view whose portals have been drawn. The entries of the sorted translucent bucket (mirror surfaces,
	// AddTranslucentPlane) are still registered, the gather of a view owns them.
	void SetDryRun(bool dryRun) { m_dryRun = dryRun; }
	// FPortalSceneState::RenderFirstSkyPortal. Returns true if a portal was drawn.
	bool RenderFirstSkyPortal(int recursion);

	// Depth of the current view (0 = main view).
	int RenderDepth() const { return m_state.RenderDepth; }
	// True while the world pass draws inside a portal stencil: the level sky needs NestedStencilPass then.
	bool InPortal() const { return !m_active.empty(); }

	// The level sky inside a portal: draws the sky geometry as a mask (drawMask), then the sky where the mask is
	// (drawSky) and restores the stencil of the portal (the level sky of LegacySkyRenderer cannot clear the stencil buffer
	// inside a portal, the mask of the portal is in it). Outside of portals the sky renderer uses its own clear.
	void NestedStencilPass(const std::function<void()>& drawMask, const std::function<void()>& drawSky);

	// Back to the table shaders (the stencil program must not stay bound for FRenderState::ClearScreen and the table shader draws).
	static void UnbindProgram();

	// ---- collection (HWWall::Process / SkyPlane / SkyLine / DoHorizon) ----

	// Everything the portal branches of HWWall::Process do for one seg of a visible subsector. frontsector / backsector are
	// the sectors of the walk after the fake flat (transfer heights) substitution, backsector null for one sided lines.
	// Returns true when the seg has been taken by a portal that replaces the wall (a mirror, a line to line portal, a horizon
	// line): the geometry renderer must not emit the middle wall of the seg then.
	bool ProcessSeg(seg_t* seg, sector_t* frontsector, sector_t* backsector, subsector_t* sub);

	// classic AddSubsectorToPortal for the ceiling and floor portal groups of a subsector that is visible in the view: the sector
	// stack portals need the subsectors for their coverage and, in out of bounds views, for their stencil. fakesector is the sector
	// of the subsector after the fake flat substitution.
	void ProcessSubsector(subsector_t* sub, sector_t* fakesector);

	// ProcessSeg for every seg of a side (static mode walks sides, not segs). Returns true if any seg was taken.
	bool ProcessSide(side_t* side, sector_t* frontsector, sector_t* backsector);
	// True when a line or a sector can produce portal surfaces at all (the cheap filter of ProcessSeg, for callers that want to skip
	// building their arguments): horizon / mirror / portal / transferred portal lines, sectors with a portal or reflective plane.
	static bool LineHasPortal(line_t* line);
	static bool SectorHasPortalPlanes(sector_t* sector);
	// The sides whose seg ProcessSeg takes (returns true for): a horizon line, a one sided line portal, a one sided line that
	// shows a skybox (transferred portal) or a mirror. The geometry renderer does not emit the middle wall of these (neither
	// dynamically nor into the static cache). View independent.
	static bool SideReplacedByPortal(const side_t* side);

	// A sector plane that is a portal (the SkyPlane branches for sector portals and plane mirrors) with the wall quad
	// ztop..zbottom already set: used by ProcessSeg and by geometry that bounds a plane itself (3D floor sides, upper and
	// lower missing textures). Plain level sky is not handled here (LegacySkyRenderer owns it): returns false then.
	bool SkyPlane(PortalSurface& surface, sector_t* sector, int plane, bool allowReflect);

	// A flat of a plane that has a sector portal or reflects (HWFlat::ProcessSector alpha rules). Returns the alpha the flat
	// has to be drawn with, 0 = do not draw the flat at all. isPortal is set when the plane has a valid sector portal.
	static float PlaneAlpha(sector_t* sector, int plane, bool* isPortal = nullptr);
	// Registers a translucent portal plane in the sorted bucket (Hooks.DrawTranslucentPlane draws it).
	void AddTranslucentPlane(sector_t* sector, int plane, float alpha, const DVector3& center);

	// gl_noskyboxes / sector portals: true when the plane of the sector is drawn by the portal driver (a sector portal that is
	// not the plain level sky), false when LegacySkyRenderer draws it as level sky.
	static bool PlaneIsPortal(sector_t* sector, int plane);

	// ---- internals the portal classes use ----

	FLevelLocals* Level() const { return m_level; }
	Data::SortedTranslucentRenderer* Sorted() const { return m_sorted; }

	// Stencil geometry (Portal::BuildWallStencil / BuildCapStencil / plane mirror geometry): appended to the shared vertex buffer.
	int StencilCount() const { return m_stencilVbo->Count(); }
	void AddStencilVertex(float x, float y, float z) { m_stencilVbo->Add(PortalStencilVertex(x, y, z)); }
	void AddStencilTriangle(const float a[3], const float b[3], const float c[3]);
	void AddStencilQuad(const float a[3], const float b[3], const float c[3], const float d[3]);
	// Binds the stencil program, the vertex stream and the MVP of the view for the draws of one stencil pass.
	void BeginStencilDraw(const PortalFrameView& view);
	void DrawStencilRange(int first, int count);
	void EndStencilDraw(const PortalFrameView& view);

	// The portals collected for the current view (classic Portals), for portals that look up others (FindPortal).
	Portal* FindPortal(const void* source);

private:
	struct Frame
	{
		PortalFrameView View;
		std::unique_ptr<FRenderViewpoint> Viewpoint;
		std::unique_ptr<PortalClip> Clip;
		std::vector<std::unique_ptr<Portal>> Portals;
	};

	FLevelLocals* m_level = nullptr;
	PortalSceneState m_state;
	bool m_dryRun = false;
	Data::SortedTranslucentRenderer* m_sorted;
	std::vector<std::unique_ptr<Frame>> m_frames;
	std::unique_ptr<Frame> m_lastFrame;	// the view EndFrame ended last (SetDryRun)
	// The portals that are rendered (the portal through which the current view is drawn is the last one).
	std::vector<Portal*> m_active;

	inline static PortalDriver* s_instance = nullptr;
	std::deque<HorizonInfo> m_uniqueHorizons;
	std::deque<secplane_t> m_uniquePlaneMirrors;

	PortalStencilProgram* m_stencilProgram = nullptr;
	std::unique_ptr<Buffer::Array::Vertex::StreamVertexBuffer<PortalStencilVertex>> m_stencilVbo;
	std::unique_ptr<VertexPipeline<PortalStencilVertex>> m_stencilPipeline;

	void EnsureStencilProgram();
	void RenderPortal(Portal* portal, bool useStencil);
	void Add(std::unique_ptr<Portal> portal);

	// PutPortal of the individual types.
	void PutHorizon(const PortalSurface& surface, const HorizonInfo& info);
	void PutSkybox(const PortalSurface& surface, FSectorPortal* secportal, int plane);
	void PutSectorStack(const PortalSurface& surface, FSectorPortalGroup* group, int plane);
	void PutPlaneMirror(const PortalSurface& surface, secplane_t* plane, int planeIndex);
	void PutMirror(const PortalSurface& surface);
	void PutLineToLine(const PortalSurface& surface, FLinePortalSpan* span);

	bool DoHorizon(seg_t* seg, sector_t* fs, vertex_t* v1, vertex_t* v2, PortalSurface& surface);
	void SkyLine(PortalSurface& surface, sector_t* fs, line_t* line);
	void SkyNormal(PortalSurface& surface, sector_t* fs, const float zfloor[2], const float zceil[2]);
	void SkyTop(PortalSurface& surface, seg_t* seg, sector_t* fs, sector_t* bs, vertex_t* v1, vertex_t* v2, const float zceil[2]);
	void SkyBottom(PortalSurface& surface, seg_t* seg, sector_t* fs, sector_t* bs, vertex_t* v1, vertex_t* v2, const float zfloor[2]);

	// The view that collects: the innermost started view, or while SetDryRun is set the view that was just ended.
	bool HasView() const { return !m_frames.empty() || (m_dryRun && m_lastFrame != nullptr); }
	const PortalFrameView& CurrentView() const { return (m_dryRun && m_lastFrame != nullptr) ? m_lastFrame->View : m_frames.back()->View; }
};

}
