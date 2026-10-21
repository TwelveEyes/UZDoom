/*
** LegacySkyRenderer.h
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
** UZDoom-only additions: GetSectorSkyInfo resolves what Helion reads from sector.SkyOptions / SkyOffset /
** Ceiling|FloorSkyTextureHandle (HWSkyInfo::init: plane sky textures, sky transfer lines, double sky, swapped skies,
** sector fade colour, skyboxes), and the *Sector* helpers wrap GetOrCreateSky / Add for the geometry renderers.
** There is no ArchiveCollection / RenderProfiler: the textures come from the LegacyGLTextureManager and the sky
** count is reported through DrawCounts (Skies).
**
*/

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include "Common/Shared/RenderInfo.h"
#include "ISkyComponent.h"
#include "SkyKey.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"

struct sector_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky
{

class LegacySkyRenderer
{
public:
	static constexpr int MaxSkyTextures = 255;

	explicit LegacySkyRenderer(Texture::Legacy::LegacyGLTextureManager* textureManager) : m_textureManager(textureManager) { }
	~LegacySkyRenderer();

	LegacySkyRenderer(const LegacySkyRenderer&) = delete;
	LegacySkyRenderer& operator=(const LegacySkyRenderer&) = delete;

	// Number of skies drawn by the last Render (Helion: RenderProfiler.DrawCounts.Skies).
	int SkiesDrawn() const { return m_skiesDrawn; }

	void Reset();
	void Clear();

	// UZDoom-only: what the sky of a plane of the sector is (HWSkyInfo::init).
	// plane: sector_t::floor or sector_t::ceiling.
	static SkyInfo GetSectorSkyInfo(sector_t* sector, int plane);

	bool GetOrCreateSky(const SkyInfo& info, ISkyComponent*& sky);
	bool GetOrCreateSectorSky(sector_t* sector, int plane, ISkyComponent*& sky);

	void Add(const Sphere::SkyGeometryVertex* data, int length, const SkyInfo& info);
	void AddSectorSky(sector_t* sector, int plane, const Sphere::SkyGeometryVertex* data, int length);

	void Render(Shared::RenderInfo& renderInfo);

	// UZDoom-only: the first sky with geometry this frame, or null.
	ISkyComponent* FirstWithGeometry() const;

	// UZDoom-only (FPortalSceneState::RenderFirstSkyPortal, gl_no_skyclear): draws one sky over the whole view without a
	// stencil and without depth, before the opaque geometry, so pixels no geometry covers (untextured one sided walls,
	// map holes) show that sky instead of the clear colour.
	static void RenderUnmasked(ISkyComponent& sky, Shared::RenderInfo& renderInfo);

	// Destroys the programs shared by all skies (renderer shutdown, color mode change).
	static void ReleaseSharedPrograms();

private:
	Texture::Legacy::LegacyGLTextureManager* m_textureManager;
	std::unordered_map<SkyKey, std::unique_ptr<ISkyComponent>, SkyKeyHash> m_skyComponents;
	std::vector<ISkyComponent*> m_skyComponentsList;
	int m_skiesDrawn = 0;
};

}
