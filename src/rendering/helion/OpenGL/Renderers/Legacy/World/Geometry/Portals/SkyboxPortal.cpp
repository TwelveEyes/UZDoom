/*
** SkyboxPortal.cpp
**
** UZDoom-only (plan M5.2), see SkyboxPortal.h. Port of HWSkyboxPortal::Setup / Shutdown.
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

#include "SkyboxPortal.h"
#include "PortalDriver.h"
#include "actor.h"
#include "g_levellocals.h"
#include "hw_renderstate.h"
#include "portal.h"
#include "r_utility.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

bool SkyboxPortal::Setup(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip)
{
	PortalSceneState& state = driver.State();
	m_oldPlaneMirrorMode = state.PlaneMirrorMode;

	if (state.SkyboxRecursion >= 3)
	{
		return false;
	}
	state.VpIsAllowedOoB = vp.bDoOob;

	state.SkyboxRecursion++;
	state.PlaneMirrorMode = 0;
	state.InSkybox = true;

	AActor* origin = m_portal->mSkybox;
	m_portal->mFlags |= PORTSF_INSKYBOX;
	vp.extralight = 0;

	m_oldClamp = screen->RenderState()->SetDepthClamp(false);
	vp.Pos = origin->InterpolatedPosition(vp.TicFrac);
	vp.ActorPos = origin->Pos();
	vp.Angles.Yaw += (origin->PrevAngles.Yaw + deltaangle(origin->PrevAngles.Yaw, origin->Angles.Yaw) * vp.TicFrac);

	// Don't let the viewpoint be too close to a floor or ceiling
	double floorh = origin->Sector->floorplane.ZatPoint(origin->Pos());
	double ceilh = origin->Sector->ceilingplane.ZatPoint(origin->Pos());
	if (vp.Pos.Z < floorh + 4) vp.Pos.Z = floorh + 4;
	if (vp.Pos.Z > ceilh - 4) vp.Pos.Z = ceilh - 4;

	vp.ViewActor = origin;

	// Skyviewpoint sectors are scaled up by 16x: mThickFogDistance /= 16, mThickFogMultiplier *= 16 (PortalClip::ApplyToUniforms).
	clip.ThickFogScale *= 16.0f;

	// di->SetupView
	vp.SetViewAngle(r_viewwindow);
	vp.OffPos = vp.Pos; // Do this after SetupView
	SetViewArea(driver.Level(), vp);
	ClearClipper(outer, vp, clip);
	UpdateCurrentMapSection(driver.Level(), vp, clip);
	return true;
}

void SkyboxPortal::Shutdown(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp)
{
	PortalSceneState& state = driver.State();
	screen->RenderState()->SetDepthClamp(m_oldClamp);

	m_portal->mFlags &= ~PORTSF_INSKYBOX;
	state.InSkybox = false;
	state.SkyboxRecursion--;
	state.PlaneMirrorMode = m_oldPlaneMirrorMode;
}

const char* SkyboxPortal::GetName() const
{
	return "Skybox";
}

}
