/*
** ScenePortal.h
**
** UZDoom-only (plan M5.2). Port of HWScenePortalBase (hw_portal.h / hw_portal.cpp): a portal whose content is the world seen from
** another viewpoint. DrawContents sets the view up (Setup), lets the world renderer draw it through PortalDriver::Hooks.RenderView
** and tears the setup down (Shutdown).
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
*/

#pragma once

#include "Portal.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

class ScenePortal : public Portal
{
public:
	bool NeedDepthBuffer() const override { return true; }
	void DrawContents(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip) override;

protected:
	// Moves vp to the viewpoint of the portal view and fills clip. Returns false if the portal is not drawn (recursion limit).
	// The classic Setup(di, rstate, clipper) with classic SetupView replaced by FRenderViewpoint::SetViewAngle (the
	// integrator builds the view matrices from vp and clip).
	virtual bool Setup(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip) = 0;
	virtual void Shutdown(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp) {}

	// HWScenePortalBase::ClearClipper: blocks everything but the angular area of the surfaces of the portal as seen from the
	// outer view (and the frustum of the portal view).
	void ClearClipper(const PortalFrameView& outer, const FRenderViewpoint& vp, PortalClip& clip) const;
	// Blocks the part of the circle outside the frustum of the portal view (the frustum clip of the mirror setup).
	static void ClipOutsideFrustum(const FRenderViewpoint& vp, PortalClip& clip);
	// classic FrustumAngle: the half angle of the horizontal frustum in BAM (0xffffffff = the view can see all around).
	static uint32_t FrustumAngle(const FRenderViewpoint& vp);
	// classic UpdateCurrentMapSection: the map section of the camera of the view is visible.
	static void UpdateCurrentMapSection(FLevelLocals* level, const FRenderViewpoint& vp, PortalClip& clip);
	// Sizes the coverage arrays of the clip (all subsectors / nodes uncovered, classic ClearBuffers of ss_renderflags / no_renderflags).
	static void InitCoverage(FLevelLocals* level, PortalClip& clip);
	// HWSectorStackPortal::SetCoverage: marks the nodes above covered subsectors, returns whether the node covers anything. The
	// covered subsectors have to be marked in clip.CoveredSubsectors (SSRF_SEEN) before.
	static uint8_t SetCoverage(FLevelLocals* level, void* node, PortalClip& clip);
	// classic SetViewArea: the render sector of the view.
	static void SetViewArea(FLevelLocals* level, FRenderViewpoint& vp);
};

}
