/*
** WeaponRenderer.h
**
** UZDoom-only (plan M4.4): port of hw_weapon.cpp (HUDSprite, PreparePlayerSprites2D/3D, PrepareTargeterSprites, DrawPSprite) on the
** EntityProgram (EntityOrientation::Hud) and the EntityModelRenderer (weapon models).
**
** Passes: Render() is called once per frame after the opaque entities and before the OIT passes. Weapon sprites with a solid
** render style are drawn right away (depth function Always, depth written near the camera, so the OIT passes reject whatever is
** behind the weapon); every other weapon item (alpha / add / fuzz / stencil / material shader styles, and everything while a weapon
** model is shown) is queued into the SortedTranslucentRenderer as its last items, so it is drawn after the OIT composite.
**
**---------------------------------------------------------------------------
**
** Copyright 2005-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#pragma once

#include <functional>
#include <memory>
#include <vector>
#include "vectors.h"
#include "palentry.h"
#include "renderstyle.h"
#include "Common/Shared/RenderInfo.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderDataPool.h"
#include "OpenGL/Renderers/Legacy/World/Data/SortedTranslucentRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityModelRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityProgram.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityVertex.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"

class AActor;
class DPSprite;
class FGameTexture;
struct FSpriteModelFrame;
struct sector_t;
class player_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

class WeaponRenderer
{
public:
	// The EntityProgram of a material shader index (GLDEFS shader on a weapon sprite), nullptr when there is none.
	using MaterialProgramLookup = std::function<EntityProgram*(int materialShaderIndex)>;

	// The program and the model renderer belong to the caller (HelionProgramRegistry / LegacyWorldRenderer).
	WeaponRenderer(Texture::Legacy::LegacyGLTextureManager& textureManager, EntityProgram* program, EntityModelRenderer* modelRenderer);
	~WeaponRenderer();

	WeaponRenderer(const WeaponRenderer&) = delete;
	WeaponRenderer& operator=(const WeaponRenderer&) = delete;

	void SetSortedRenderer(Data::SortedTranslucentRenderer* sortedRenderer) { m_sortedRenderer = sortedRenderer; }
	void SetMaterialProgramLookup(MaterialProgramLookup lookup) { m_materialProgramLookup = std::move(lookup); }

	// Releases the instances of the previous frame. Render() calls it first; call it too when the frame is skipped.
	void Clear();

	// PreparePlayerSprites + DrawPlayerSprites(hudModelStep). The player and the viewpoint are r_viewpoint / players[consoleplayer];
	// the view sector, transfer heights view and uniforms come from the RenderInfo, which has to stay valid until the sorted
	// renderer ran. The solid sprites are drawn right away, the others (and the models) are queued into the sorted renderer, so
	// call it after SortedTranslucentRenderer::Clear() and render the sorted renderer after it. the classic renderer's order: the HUD
	// model step (hudModelStep true) after the scene before the post processing, the 2D step in the 2D callback after bloom.
	void Render(Shared::RenderInfo& renderInfo, bool hudModelStep);

private:
	using GLLegacyTexture = Texture::Legacy::GLLegacyTexture;

	// HUDSprite of hw_weapon.h.
	struct HudItem
	{
		DPSprite* Weapon = nullptr;
		FSpriteModelFrame* ModelFrame = nullptr;
		FGameTexture* GameTexture = nullptr;
		GLLegacyTexture* Texture = nullptr;
		GLLegacyTexture* Brightmap = nullptr;

		FRenderStyle RenderStyle;
		float Alpha = 1.0f;
		int OverrideFuzz = 0;				// gl_fuzztype of the fuzz override shader, 0 = none
		PalEntry ObjectColor = 0xffffffff;
		PalEntry AddColor = 0;
		int LightLevel = 255;
		bool Fullbright = false;			// SetBright
		bool NoColoredLight = false;
		int Desaturation = 0;
		int LightBufferIndex = 0;
		float DynRgb[3] = { 0, 0, 0 };
		bool HasDyn = false;
		bool PerPixelLights = false;		// weapon models
		int Translation = 0;

		// sprites: the four corners in screen pixels (left top, left bottom, right top, right bottom)
		float Corner[4][2] = {};
		bool FlipU = false;

		// models
		FVector3 ModelTranslation, ModelRotation, ModelPivot;
		float Mx = 0, My = 0;
	};

	struct WeaponLight
	{
		int LightLevel = 255;
		int LightBufferIndex = 0;
		int Desaturation = 0;
		bool IsBelow = false;
		bool NoColoredLight = false;
	};

	Texture::Legacy::LegacyGLTextureManager& m_textureManager;
	EntityProgram* m_program;
	EntityModelRenderer* m_modelRenderer;
	Data::SortedTranslucentRenderer* m_sortedRenderer = nullptr;
	MaterialProgramLookup m_materialProgramLookup;
	std::unique_ptr<Data::RenderDataPool<EntityVertex>> m_pool;
	std::vector<Data::RenderData<EntityVertex>*> m_used;
	std::vector<HudItem> m_items;
	FullbrightState m_fullbright;
	bool m_paletteColorMode = false;
	int m_sortCounter = 0;
	bool m_depthCleared = false;

	WeaponLight GetWeaponLighting(Shared::RenderInfo& renderInfo, sector_t* viewsector, const DVector3& pos, bool fullbrightScene, const DVector3& playerpos);
	bool GetWeaponRenderStyle(HudItem& item, DPSprite* psp, AActor* playermo, sector_t* viewsector, const WeaponLight& light, double ticFrac);
	bool GetWeaponRect(HudItem& item, DPSprite* psp, float sx, float sy, double ticfrac);
	void PreparePlayerSprites2D(Shared::RenderInfo& renderInfo, sector_t* viewsector);
	void PreparePlayerSprites3D(Shared::RenderInfo& renderInfo, sector_t* viewsector);
	void PrepareTargeterSprites(Shared::RenderInfo& renderInfo, sector_t* viewsector, double ticFrac);

	EntityVertex MakeVertex(const HudItem& item, float* sortKeyUnused);
	bool IsSolid(const HudItem& item) const;
	void SetUniforms(EntityProgram& program, Shared::RenderInfo& renderInfo, const HudItem& item, bool material);
	void DrawSprite(Shared::RenderInfo& renderInfo, Data::RenderData<EntityVertex>* data, const HudItem& item, EntityProgram* program, bool material, bool sorted);
	void DrawModel(Shared::RenderInfo& renderInfo, const HudItem& item, bool clearDepth);
	void Submit(Shared::RenderInfo& renderInfo, const HudItem& item);
};

}
