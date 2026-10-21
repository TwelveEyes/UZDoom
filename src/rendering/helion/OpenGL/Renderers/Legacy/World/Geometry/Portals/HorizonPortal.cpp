/*
** HorizonPortal.cpp
**
** UZDoom-only (plan M5.2), see HorizonPortal.h. Port of the constructor and DrawContents of HWHorizonPortal.
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

#include "HorizonPortal.h"
#include "gametexture.h"
#include "hw_renderstate.h"
#include "r_utility.h"
#include "texturemanager.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

HorizonPortal::HorizonPortal(const HorizonInfo* info, const FRenderViewpoint& vp) : m_origin(info)
{
	// create the vertex data for this horizon portal.
	const HorizonPlane* sp = &m_origin->Plane;
	const float vx = vp.Pos.X;
	const float vy = vp.Pos.Y;
	const float vz = vp.Pos.Z;
	const float z = sp->Texheight;
	const float tz = (z - vz);

	// Draw to some far away boundary
	// This is not drawn as larger strips because it causes visual glitches.
	m_strips.reserve(1024);
	for (int xx = -32768; xx < 32768; xx += 4096)
	{
		float x = xx + vx;
		for (int yy = -32768; yy < 32768; yy += 4096)
		{
			float y = yy + vy;
			m_strips.push_back({ x, y, z, x / 64, -y / 64 });
			m_strips.push_back({ x + 4096, y, z, x / 64 + 64, -y / 64 });
			m_strips.push_back({ x, y + 4096, z, x / 64, -y / 64 - 64 });
			m_strips.push_back({ x + 4096, y + 4096, z, x / 64 + 64, -y / 64 - 64 });
		}
	}

	// fill the gap between the polygon and the true horizon
	// Since I can't draw into infinity there can always be a
	// small gap
	m_skirt.reserve(10);
	m_skirt.push_back({ -32768 + vx, -32768 + vy, z, 512.f, 0 });
	m_skirt.push_back({ -32768 + vx, -32768 + vy, vz, 512.f, tz });
	m_skirt.push_back({ -32768 + vx, 32768 + vy, z, -512.f, 0 });
	m_skirt.push_back({ -32768 + vx, 32768 + vy, vz, -512.f, tz });
	m_skirt.push_back({ 32768 + vx, 32768 + vy, z, 512.f, 0 });
	m_skirt.push_back({ 32768 + vx, 32768 + vy, vz, 512.f, tz });
	m_skirt.push_back({ 32768 + vx, -32768 + vy, z, -512.f, 0 });
	m_skirt.push_back({ 32768 + vx, -32768 + vy, vz, -512.f, tz });
	m_skirt.push_back({ -32768 + vx, -32768 + vy, z, 512.f, 0 });
	m_skirt.push_back({ -32768 + vx, -32768 + vy, vz, 512.f, tz });
}

void HorizonPortal::DrawContents(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip)
{
	FGameTexture* texture = TexMan.GetGameTexture(m_origin->Plane.Texture, true);

	if (!texture || !texture->isValid())
	{
		driver.UnbindProgram();
		screen->RenderState()->ClearScreen();
		return;
	}

	if (driver.Hooks.DrawHorizon)
	{
		HorizonDraw draw;
		draw.Info = m_origin;
		draw.Strips = &m_strips;
		draw.Skirt = &m_skirt;
		draw.Viewpoint = &vp;
		driver.UnbindProgram();
		driver.Hooks.DrawHorizon(draw);
	}
}

const char* HorizonPortal::GetName() const
{
	return "Horizon";
}

}
