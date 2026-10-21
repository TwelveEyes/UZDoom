/*
** RenderInfo.h
**
** Port of Helion.Render.OpenGL.Shared.RenderInfo.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** ConfigRender has no equivalent object: the renderer settings are CVars
** (plan D16/D20/D28), so the Config member is dropped. Viewport is an
** IntRect, Entity an AActor*, Sector a sector_t*.
*/

#pragma once

#include "Common/Shared/OldCamera.h"
#include "OpenGL/Renderers/Legacy/World/ShaderUniforms.h"
#include "World/Geometry/Sectors/TransferHeightsView.h"
#include "intrect.h"

class AActor;
struct sector_t;
struct HWViewpointUniforms;

namespace Helion::Render::OpenGL::Shared
{

/// <summary>
/// A simple container for render-related information.
/// </summary>
class RenderInfo
{
public:
	static inline Geometry::Vec2F LastAutomapOffset = Geometry::Vec2F(0, 0);

	OldCamera* Camera;
	float TickFraction = 0;
	IntRect Viewport = {};
	AActor* ViewerEntity;
	bool DrawAutomap = false;
	Geometry::Vec2I AutomapOffset = Geometry::Vec2I(0, 0);
	double AutomapScale = 0;
	Renderers::Legacy::World::ShaderUniforms Uniforms;
	sector_t* ViewSector;
	::Helion::World::Geometry::Sectors::TransferHeightView TransferHeightView = ::Helion::World::Geometry::Sectors::TransferHeightView::Top;
	bool BrightMaps = false;
	bool SectorColor = false;
	bool SectorFog = false;

	// UZDoom-only view state (Helion reads these from ConfigRender / the player object), set by Renderer from
	// Renderer::SetView before the world command is handled.
	// Horizontal field of view (degrees), aspect ratio and fov ratio as passed to VREyeInfo::GetProjection.
	float FieldOfView = 90;
	float AspectRatio = 4.0f / 3.0f;
	float FovRatio = 4.0f / 3.0f;
	// Stereo / orthographic views supply the finished projection matrix instead.
	bool HasProjectionOverride = false;
	Geometry::mat4 ProjectionOverride;
	// Level->info->pixelstretch of the viewed level (Helion's fixed 0.825 height factor).
	float PixelStretch = 1.2f;
	// Player view state: Player.DrawFullBright(), DrawInvulnerableColorMap(), HasLightAmp(), GetExtraLightRender().
	bool DrawFullBright = false;
	bool DrawInvulnerableColorMap = false;
	bool HasLightAmp = false;
	int ExtraLight = 0;
	// FRenderViewpoint::showviewer: draw the viewer's own actor (chase camera, camera textures).
	bool ShowViewer = false;
	// Out of bounds camera with the radar clipper (plan D27) or r_dithertransparency, or r_distance_cull_type: the view has to be
	// traversed with the BSP.
	bool ForceBsp = false;
	// The player's view (Helion_RenderViewpoint mainview): not a camera texture / save picture / portal view. Only it marks the automap.
	bool MainView = false;
	// FRenderViewpoint::culldist when r_distance_cull_type is on, else 0 (whole map).
	double CullDistance = 0;
	// The viewpoint block the programs read (contract uniforms); owned by Renderer, valid for the frame.
	const HWViewpointUniforms* Viewpoint = nullptr;
	// Portal views through mirrors (plan M5.2): the image is flipped horizontally (MirrorX, mirror portals) and / or vertically
	// (MirrorY, plane mirrors) in clip space; ProjectionMatrix() includes the flip, FovY / ProjectionAspect do not.
	bool MirrorX = false;
	bool MirrorY = false;

	RenderInfo();

	// The projection of the viewpoint that is rendered (UZDoom: VREyeInfo::GetProjection of the eye, as hw_entrypoint
	// builds it; Helion's fixed 63.2 degree projection is not used). Every renderer that needs a projection (world MVP,
	// sky sphere / skybox, portals) takes it from here.
	Geometry::mat4 ProjectionMatrix() const;
	// The projection without the mirror flip (the HWViewpointUniforms block of a mirror view carries the flip in its view matrix).
	Geometry::mat4 UnmirroredProjectionMatrix() const;
	// The same view with other clip planes (sky). Orthographic / stereo projections keep their matrix.
	Geometry::mat4 ProjectionMatrix(float zNear, float zFar) const;
	// Vertical field of view in radians of the projection (2 * atan(1 / P[1][1])).
	float FovY() const;
	// Width / height of the projection (P[1][1] / P[0][0]).
	float ProjectionAspect() const;

	void Set(OldCamera* camera, float tickFraction, const IntRect& viewport, AActor* viewerEntity, bool drawAutomap,
		const Geometry::Vec2I& automapOffset, double automapScale, sector_t* viewSector,
		::Helion::World::Geometry::Sectors::TransferHeightView transferHeightView,
		bool brightMaps, bool sectorColor, bool sectorFog);
};

}
