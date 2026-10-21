/*
** LegacySkyRenderer.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.LegacySkyRenderer.
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

#include <algorithm>
#include "c_cvars.h"
#include "g_levellocals.h"
#include "gametexture.h"
#include "p_lnspec.h"
#include "hw_renderstate.h"
#include "r_defs.h"
#include "v_video.h"
#include "skyboxtexture.h"
#include "texturemanager.h"
#include "LegacySkyRenderer.h"
#include "SkyBox/SkyBoxComponent.h"
#include "Sphere/SkySphereComponent.h"
#include "Sphere/SkySphereRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalDriver.h"

EXTERN_CVAR(Int, r_distance_cull_type)
EXTERN_CVAR(Color, gl_cullcolor)

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky
{

using Helion::Resources::Definitions::SkyOptions;

LegacySkyRenderer::~LegacySkyRenderer()
{
	Reset();
}

void LegacySkyRenderer::Reset()
{
	m_skyComponentsList.clear();
	m_skyComponents.clear();
}

void LegacySkyRenderer::Clear()
{
	for (ISkyComponent* sky : m_skyComponentsList)
		sky->Clear();
}

void LegacySkyRenderer::ReleaseSharedPrograms()
{
	Sphere::SkySphereShared::Release();
	SkyBox::SkyBoxComponent::ReleaseShared();
}

static FTextureID PlaneSkyTexture(sector_t* sector, int plane, bool second)
{
	FTextureID tex = sector->planes[plane].skytexture[second];
	if (tex.isValid())
		return tex;
	return second ? sector->Level->skytexture2 : sector->Level->skytexture1;
}

// UZDoom: HWSkyInfo::init (hw_sky.cpp), expressed as a SkyInfo. The sector's skytransfer and the plane skytexture[]
// (UDMF skyfloor / skyceiling) take the place of Helion's sector.SkyOptions / SkyOffset / *SkyTextureHandle.
SkyInfo LegacySkyRenderer::GetSectorSkyInfo(sector_t* sector, int plane)
{
	FLevelLocals* level = sector->Level;
	SkyInfo info;
	int sky1 = sector->skytransfer;

	// Sky transfer: MBF sky transfer / ZDoom Static_Init sky; the line's front upper (or lower with swapped skies) texture.
	bool resolved = false;
	if ((sky1 & PL_SKYFLAT) && (sky1 & (PL_SKYFLAT - 1)))
	{
		const line_t* line = &level->lines[(sky1 & (PL_SKYFLAT - 1)) - 1];
		const side_t* side = line->sidedef[0];
		int pos = side_t::top;
		if ((level->flags & LEVEL_SWAPSKIES) && side->GetTexture(side_t::bottom).isValid())
			pos = side_t::bottom;

		FTextureID texno = side->GetTexture(pos);
		FGameTexture* tex = TexMan.GetGameTexture(texno, true);
		if (tex != nullptr && tex->isValid())
		{
			info.Texture = texno.GetIndex();
			info.ScrollLayer = SkyInfo::NoScroll;
			info.Offset = ::Helion::Geometry::Vec2F((float)(side->GetTextureXOffset(pos) * (360.0 / 65536.0)), (float)side->GetTextureYOffset(pos));
			info.Options = SkyOptions::SkyTransfer;
			// Helion SpecialManager.SetStaticInit: Flip when Arg2 != 0. Helion's Flip is the normal sky orientation, i.e.
			// HWSkyInfo's mirrored == !line->args[2] is "no Flip".
			if (line->args[2])
				info.Options |= SkyOptions::Flip;
			resolved = true;
		}
	}

	if (!resolved)
	{
		FTextureID skytex1 = PlaneSkyTexture(sector, plane, false);
		FTextureID skytex2 = PlaneSkyTexture(sector, plane, true);
		info.Options = SkyOptions::Flip;
		bool doublesky = (level->flags & LEVEL_DOUBLESKY) != 0;

		if (doublesky)
		{
			// The front layer is the first sky, scrolled with sky speed 1.
			info.ForegroundTexture = skytex1.GetIndex();
		}

		if (((level->flags & LEVEL_SWAPSKIES) || (sky1 == PL_SKYFLAT) || doublesky) && skytex2 != skytex1)
		{
			// If both skies are equal use the scroll offset of the first!
			info.Texture = skytex2.GetIndex();
			info.ScrollLayer = 1;
		}
		else if (!doublesky)
		{
			// HWSkyInfo::init: texture[0] is the plane's sky (UDMF skyfloor / skyceiling), falling back to sky1.
			info.Texture = skytex1.GetIndex();
			info.ScrollLayer = 0;
		}
		else
		{
			// Double sky with identical layers (UZDoom draws the front layer only).
			info.Texture = skytex1.GetIndex();
			info.ForegroundTexture = SkyInfo::NoTexture;
			info.ScrollLayer = 0;
		}
	}

	PalEntry fade = sector->Colormap.FadeColor;
	if (r_distance_cull_type > 1)
		info.FadeColor = (fade.d & 0xffffff) != 0 ? (uint32_t)(fade.d & 0xffffff) : (uint32_t)(PalEntry(gl_cullcolor).d & 0xffffff);
	else if (level->skyfog > 0)
		info.FadeColor = (uint32_t)(fade.d & 0xffffff);
	else
		info.FadeColor = 0;
	// RenderFog needs to know "colour present" separately from "black"; keep a flag bit in the unused alpha byte.
	if (info.FadeColor != 0 || r_distance_cull_type > 1)
		info.FadeColor |= 0xff000000u;

	FGameTexture* mainTexture = TexMan.GetGameTexture(FSetTextureID(info.Texture), true);
	info.IsSkyBox = mainTexture != nullptr && mainTexture->GetTexture() != nullptr && dynamic_cast<FSkyBox*>(mainTexture->GetTexture()) != nullptr;
	if (info.IsSkyBox)
		info.ForegroundTexture = SkyInfo::NoTexture;
	return info;
}

bool LegacySkyRenderer::GetOrCreateSky(const SkyInfo& info, ISkyComponent*& sky)
{
	SkyKey key(info);
	auto it = m_skyComponents.find(key);
	if (it != m_skyComponents.end())
	{
		sky = it->second.get();
		return true;
	}

	if (m_skyComponentsList.size() >= (size_t)MaxSkyTextures)
	{
		sky = nullptr;
		return false;
	}

	std::unique_ptr<ISkyComponent> component;
	if (info.IsSkyBox)
		component = std::make_unique<SkyBox::SkyBoxComponent>(m_textureManager, info);
	else
		component = std::make_unique<Sphere::SkySphereComponent>(m_textureManager, info);

	sky = component.get();
	m_skyComponentsList.push_back(sky);
	m_skyComponents[key] = std::move(component);
	return true;
}

bool LegacySkyRenderer::GetOrCreateSectorSky(sector_t* sector, int plane, ISkyComponent*& sky)
{
	return GetOrCreateSky(GetSectorSkyInfo(sector, plane), sky);
}

void LegacySkyRenderer::Add(const Sphere::SkyGeometryVertex* data, int length, const SkyInfo& info)
{
	ISkyComponent* sky;
	if (!GetOrCreateSky(info, sky))
		return;

	sky->Add(data, length);
}

void LegacySkyRenderer::AddSectorSky(sector_t* sector, int plane, const Sphere::SkyGeometryVertex* data, int length)
{
	// UZDoom (plan M5.2): a sky plane with a sector portal (skybox) is drawn by the portal driver, not as level sky.
	if (Geometry::Portals::PortalDriver::PlaneIsPortal(sector, plane))
		return;
	Add(data, length, GetSectorSkyInfo(sector, plane));
}

ISkyComponent* LegacySkyRenderer::FirstWithGeometry() const
{
	for (ISkyComponent* sky : m_skyComponentsList)
	{
		if (sky->HasGeometry())
			return sky;
	}
	return nullptr;
}

void LegacySkyRenderer::RenderUnmasked(ISkyComponent& sky, Shared::RenderInfo& renderInfo)
{
	FRenderState& state = *screen->RenderState();
	state.EnableStencil(false);
	state.EnableDepthTest(false);
	sky.RenderSky(renderInfo);
	state.EnableDepthTest(true);
}

void LegacySkyRenderer::Render(Shared::RenderInfo& renderInfo)
{
	m_skiesDrawn = 0;

	bool anyGeometry = false;
	for (ISkyComponent* sky : m_skyComponentsList)
	{
		if (sky->HasGeometry())
		{
			anyGeometry = true;
			break;
		}
	}

	if (!anyGeometry)
		return;

	FRenderState& state = *screen->RenderState();

	// UZDoom (plan M5.2): inside a portal the stencil buffer holds the portal mask, the sky must not clear it.
	Geometry::Portals::PortalDriver* portals = Geometry::Portals::PortalDriver::Instance();
	if (portals != nullptr && portals->InPortal())
	{
		for (ISkyComponent* sky : m_skyComponentsList)
		{
			if (!sky->HasGeometry())
				continue;
			portals->NestedStencilPass([&] { sky->RenderWorldGeometry(renderInfo); }, [&] { sky->RenderSky(renderInfo); });
			m_skiesDrawn++;
		}
		return;
	}

	// Helion: each sky with geometry gets its own stencil pass. The geometry increments the stencil of the pixels
	// that pass the depth test (equal 0 -> 1, so overdraw does not count twice), then the sky is drawn where the
	// stencil is 1 with the depth test off.
	state.EnableStencil(true);
	for (ISkyComponent* sky : m_skyComponentsList)
	{
		if (!sky->HasGeometry())
			continue;

		state.Clear(CT_Stencil);
		state.SetStencil(0, SOP_Increment, SF_ColorMaskOff);
		state.SetColorMask(false, false, false, false);

		sky->RenderWorldGeometry(renderInfo);

		state.SetColorMask(true, true, true, true);
		state.SetStencil(1, SOP_Keep, SF_AllOn);
		state.EnableDepthTest(false);

		sky->RenderSky(renderInfo);

		state.EnableDepthTest(true);
		m_skiesDrawn++;
	}

	state.SetStencil(0, SOP_Keep, SF_AllOn);
	state.EnableStencil(false);
}

}
