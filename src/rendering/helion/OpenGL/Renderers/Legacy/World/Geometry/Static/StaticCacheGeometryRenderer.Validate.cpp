/*
** StaticCacheGeometryRenderer.Validate.cpp
**
** UZDoom-only partial of StaticCacheGeometryRenderer: the per-frame render signature sweep (plan D14).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The events of the world hooks (SectorMoveStart / Complete, PlaneTransformed, PlaneTextureChanged, SideTextureChanged)
** are raised from the thinkers and setters; everything that writes sector_t / side_t members directly (ACS / ZScript
** natives, UDMF triggers, savegame loads, mods) raises nothing. sector_t::renderSignature / side_t::renderSignature hold
** the values the static cache last built its geometry for; this sweep compares them with the current values, routes the
** differences through the same handlers as the events and stores the new values.
** Helion's SectorMove (every tick a control sector of a 3D floor moves) and SectorFogColorChanged are not raised by the
** playsim hooks either, so the sweep raises them too.
** Light level changes are not part of the static geometry (the light is in the sector light buffers), they are only
** stored; so are the wall flags.
**
*/

#include "StaticCacheGeometryRenderer.h"

#include "g_levellocals.h"
#include "r_defs.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

using ::Helion::World::Static::RenderStaticState;

void StaticCacheGeometryRenderer::TakeSectorSignature(sector_t* sector, FSectorRenderSignature& signature)
{
	signature.floorD = sector->floorplane.fD();
	signature.ceilD = sector->ceilingplane.fD();
	signature.floorTex = sector->GetTexture(sector_t::floor).GetIndex();
	signature.ceilTex = sector->GetTexture(sector_t::ceiling).GetIndex();
	signature.light = (int16_t)sector->lightlevel;
	signature.color = sector->Colormap.LightColor.d;
	signature.fade = sector->Colormap.FadeColor.d;
	for (int pos = 0; pos < 2; pos++)
	{
		signature.xoff[pos] = (float)sector->GetXOffset(pos);
		signature.yoff[pos] = (float)sector->GetYOffset(pos);
		signature.xscale[pos] = (float)sector->GetXScale(pos);
		signature.yscale[pos] = (float)sector->GetYScale(pos);
		signature.angle[pos] = (float)sector->GetAngle(pos).Degrees();
	}
}

void StaticCacheGeometryRenderer::TakeSideSignature(side_t* side, FSideRenderSignature& signature)
{
	for (int tier = 0; tier < 3; tier++)
	{
		signature.tex[tier] = side->textures[tier].texture.GetIndex();
		signature.xoff[tier] = (float)side->GetTextureXOffset(tier);
		signature.yoff[tier] = (float)side->GetTextureYOffset(tier);
		signature.xscale[tier] = (float)side->GetTextureXScale(tier);
		signature.yscale[tier] = (float)side->GetTextureYScale(tier);
	}

	signature.flags = (uint32_t)side->Flags;
	signature.light = side->Light;
}

void StaticCacheGeometryRenderer::RefreshPlaneSignature(sector_t* sector, int pos)
{
	if (!m_signaturesValid)
		return;

	FSectorRenderSignature current;
	TakeSectorSignature(sector, current);
	FSectorRenderSignature& cached = sector->renderSignature;
	if (pos == sector_t::floor)
	{
		cached.floorD = current.floorD;
		cached.floorTex = current.floorTex;
	}
	else
	{
		cached.ceilD = current.ceilD;
		cached.ceilTex = current.ceilTex;
	}

	cached.xoff[pos] = current.xoff[pos];
	cached.yoff[pos] = current.yoff[pos];
	cached.xscale[pos] = current.xscale[pos];
	cached.yscale[pos] = current.yscale[pos];
	cached.angle[pos] = current.angle[pos];
}

bool StaticCacheGeometryRenderer::PlaneSignatureChanged(sector_t* sector, int pos)
{
	if (!m_signaturesValid)
		return true;

	FSectorRenderSignature current;
	TakeSectorSignature(sector, current);
	const FSectorRenderSignature& cached = sector->renderSignature;
	bool floor = pos == sector_t::floor;
	return (floor ? current.floorD != cached.floorD : current.ceilD != cached.ceilD) ||
		(floor ? current.floorTex != cached.floorTex : current.ceilTex != cached.ceilTex) ||
		current.xoff[pos] != cached.xoff[pos] || current.yoff[pos] != cached.yoff[pos] || current.xscale[pos] != cached.xscale[pos] ||
		current.yscale[pos] != cached.yscale[pos] || current.angle[pos] != cached.angle[pos];
}

void StaticCacheGeometryRenderer::RefreshSideSignature(side_t* side, int tier)
{
	if (!m_signaturesValid)
		return;

	FSideRenderSignature current;
	TakeSideSignature(side, current);
	FSideRenderSignature& cached = side->renderSignature;
	cached.tex[tier] = current.tex[tier];
	cached.xoff[tier] = current.xoff[tier];
	cached.yoff[tier] = current.yoff[tier];
	cached.xscale[tier] = current.xscale[tier];
	cached.yscale[tier] = current.yscale[tier];
}

void StaticCacheGeometryRenderer::InitSignatures()
{
	for (unsigned i = 0; i < m_world->sectors.Size(); i++)
		TakeSectorSignature(&m_world->sectors[i], m_world->sectors[i].renderSignature);

	for (unsigned i = 0; i < m_world->sides.Size(); i++)
		TakeSideSignature(&m_world->sides[i], m_world->sides[i].renderSignature);

	m_signaturesValid = true;
}

void StaticCacheGeometryRenderer::ValidateSignatures()
{
	if (!m_signaturesValid || m_world == nullptr)
		return;

	m_geometryRenderer.SetBuffer(false);

	for (unsigned i = 0; i < m_world->sectors.Size(); i++)
	{
		sector_t* sector = &m_world->sectors[i];
		FSectorRenderSignature current;
		TakeSectorSignature(sector, current);
		FSectorRenderSignature& cached = sector->renderSignature;

		double planeD[2] = { current.floorD, current.ceilD };
		double cachedPlaneD[2] = { cached.floorD, cached.ceilD };
		int tex[2] = { current.floorTex, current.ceilTex };
		int cachedTex[2] = { cached.floorTex, cached.ceilTex };

		// Stored first: the handlers below rebuild geometry from the sector, they do not read the signature.
		uint32_t previousFade = cached.fade;
		FSectorRenderSignature previous = cached;
		cached = current;

		for (int pos = 0; pos < 2; pos++)
		{
			const bool moving = (sector->renderDynamic[pos] & SD_Movement) != 0;

			if (tex[pos] != cachedTex[pos])
				World_PlaneTextureChanged(sector, pos, FSetTextureID(tex[pos]), FSetTextureID(cachedTex[pos]));

			bool transformed = planeD[pos] != cachedPlaneD[pos] || previous.xoff[pos] != current.xoff[pos] || previous.yoff[pos] != current.yoff[pos] ||
				previous.xscale[pos] != current.xscale[pos] || previous.yscale[pos] != current.yscale[pos] || previous.angle[pos] != current.angle[pos];

			if (!transformed)
				continue;

			// Control sectors of 3D floors: Helion's SectorMove, every tick of the move.
			if (planeD[pos] != cachedPlaneD[pos] && m_taggedSectors3D.find(sector->Index()) != m_taggedSectors3D.end())
				World_SectorMove(sector);

			// The mover (or scroller) keeps the plane dynamic and rebuilds it when it completes.
			if (moving || (sector->renderDynamic[pos] & SD_Scroll) != 0)
				continue;

			World_SectorPlaneTransformed(sector, pos);
		}

		World_SectorFogColorChanged(sector, previousFade);
	}

	for (unsigned i = 0; i < m_world->sides.Size(); i++)
	{
		side_t* side = &m_world->sides[i];
		FSideRenderSignature current;
		TakeSideSignature(side, current);
		FSideRenderSignature previous = side->renderSignature;
		side->renderSignature = current;

		// Scrolling walls are rendered dynamically and have no cached geometry; polyobject sides are never cached.
		if ((side->renderDynamic & (SD_Scroll | SD_Polyobject)) != 0 || (side->Flags & WALLF_POLYOBJ) != 0)
			continue;

		for (int tier = 0; tier < 3; tier++)
		{
			bool changed = previous.tex[tier] != current.tex[tier] || previous.xoff[tier] != current.xoff[tier] || previous.yoff[tier] != current.yoff[tier] ||
				previous.xscale[tier] != current.xscale[tier] || previous.yscale[tier] != current.yscale[tier];
			if (!changed)
				continue;

			// The offsets and scales are part of the wall UV, a rebuild of the wall is the same as for a texture change.
			World_SideTextureChanged(side, tier, FSetTextureID(current.tex[tier]), FSetTextureID(previous.tex[tier]));
		}
	}
}

}
