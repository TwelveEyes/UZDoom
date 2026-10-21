/*
** EntityRenderer.VisualThinkers.cpp
**
** VisualThinkers of EntityRenderer: HWSprite::AdjustVisualThinker (a DVisualThinker that renders as a textured sprite,
** drawn like an actor sprite with the particle rules for billboarding). VisualThinkers that render as particles go
** through EntityRenderer::RenderParticle.
**
**---------------------------------------------------------------------------
**
** Copyright 2002-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include "r_defs.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityRenderer.h"
#include "BindTextures.h"
#include "HelionCVars.h"
#include "Util/Constants.h"
#include "World/Geometry/Sectors/RenderSector.h"
#include "Common/Shared/World/ViewClipping/ViewClipper.h"
#include "actor.h"
#include "actorinlines.h"
#include "a_pickups.h"
#include "d_net.h"
#include "d_player.h"
#include "g_levellocals.h"
#include "gametexture.h"
#include "hw_cvars.h"
#include "hw_renderstate.h"
#include "matrix.h"
#include "p_3dfloors.h"
#include "p_effect.h"
#include "r_data/r_vanillatrans.h"
#include "r_data/sprites.h"
#include "r_utility.h"
#include "texturemanager.h"
#include "v_video.h"

extern uint32_t r_renderercaps;

EXTERN_CVAR(Bool, r_debug_disable_vis_filter)
EXTERN_CVAR(Float, transsouls)
EXTERN_CVAR(Float, r_actorspriteshadowalpha)
EXTERN_CVAR(Float, r_actorspriteshadowfadeheight)
EXTERN_CVAR(Float, r_actorspriteshadowdist)
EXTERN_CVAR(Int, r_distance_cull_type)
EXTERN_CVAR(Float, r_spriteclipanamorphicminbias)
EXTERN_CVAR(Bool, r_debug_nolimitanamorphoses)
EXTERN_CVAR(Int, r_extralight)
EXTERN_CVAR(Bool, gl_usecolorblending)
EXTERN_CVAR(Bool, gl_sprite_blend)
EXTERN_CVAR(Int, gl_spriteclip)
EXTERN_CVAR(Float, gl_sclipthreshold)
EXTERN_CVAR(Float, gl_sclipfactor)
EXTERN_CVAR(Int, gl_billboard_mode)
EXTERN_CVAR(Bool, gl_billboard_faces_camera)
EXTERN_CVAR(Bool, hw_force_cambbpref)
EXTERN_CVAR(Int, gl_fuzztype)
EXTERN_CVAR(Int, gl_particles_style)
EXTERN_CVAR(Bool, gl_billboard_particles)
EXTERN_CVAR(Bool, gl_light_sprites)
EXTERN_CVAR(Bool, gl_light_particles)
EXTERN_CVAR(Bool, gl_enhanced_nightvision)
EXTERN_CVAR(Float, gl_mask_sprite_threshold)

#include "Common/Shared/Frustum.h"
#include "p_effect.h"
#include "p_visualthinker.h"
#include "textures/animations.h"
EXTERN_CVAR(Bool, r_particles)

using namespace Helion::Geometry;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

namespace Enums = ::Helion::Resources::Definitions::Decorate::Properties::Enums;

void EntityRenderer::RenderVisualThinker(DVisualThinker* spr, sector_t* sector, int renderIndex)
{
	const auto& vp = r_viewpoint;
	FLevelLocals* level = sector->Level;
	m_level = level;
	particle_t* particle = &spr->PT;

	double timefrac = Net_ModifyObjectFrac(spr, vp.TicFrac);
	if (paused || spr->isFrozen())
		timefrac = 0.;

	const bool customAnim = ((spr->PT.flags & SPF_LOCAL_ANIM) && spr->PT.animData.ok);
	FTextureID patch = customAnim ? TexAnim.UpdateStandaloneAnimation(spr->PT.animData, level->LocalWorldTimer + timefrac) : spr->PT.texture;
	if (!patch.isValid())
		return;
	auto gameTexture = TexMan.GetGameTexture(patch, !customAnim);
	if (!gameTexture || !gameTexture->isValid())
		return;

	const int translation = spr->Translation.index();
	const auto* spriteRotation = m_textureManager.GetSpriteRotation(patch, false, translation);
	if (spriteRotation == nullptr || spriteRotation->RenderStore == nullptr)
		return;

	GLLegacyTexture* texture = spriteRotation->RenderStore;
	GLLegacyTexture* brightmapTexture = spriteRotation->BrightmapRenderStore;

	const bool fullbright = !!(particle->flags & SPF_FULLBRIGHT);
	const bool flip = !!(spr->flags & VTF_FlipX);
	const bool yflip = !!(spr->flags & VTF_FlipY);

	// ---- position ----
	DVector3 pos = particle->Pos;
	DVector3 prevPos = (spr->flags & VTF_DontInterpolate) || timefrac == 0. ? particle->Pos : spr->Prev;
	if (spr->flags & VTF_DontInterpolate)
		prevPos = pos;

	const TextureGeometry geometry = GetTextureGeometry(texture, flip);
	double scaleX = spr->Scale.X * geometry.FoldX;
	double scaleY = spr->Scale.Y * geometry.FoldY;
	if ((spr->PT.flags & SPF_ROLL) && !(spr->PT.flags & SPF_STRETCHPIXELS))
	{
		double ps = level->pixelstretch;
		double mult = 1.0 / sqrt(ps); // shrink slightly
		scaleX *= mult * ps;
		scaleY *= mult;
	}

	// ---- style ----
	float trans = particle->alpha;
	FRenderStyle renderStyle = particle->style != STYLE_None ? LegacyRenderStyles[particle->style] : LegacyRenderStyles[STYLE_Translucent];
	if (renderStyle.BlendOp >= STYLEOP_Fuzz && renderStyle.BlendOp <= STYLEOP_FuzzOrRevSub)
	{
		renderStyle.CheckFuzz();
		if (renderStyle.BlendOp == STYLEOP_Fuzz)
			renderStyle.BlendOp = STYLEOP_Shadow;
	}
	if (!m_spriteAlpha)
	{
		renderStyle = LegacyRenderStyles[STYLE_Normal];
		trans = 1.f;
	}

	PalEntry thingColor(particle->color);
	thingColor.a = 255;

	// [BB] Translucent particles have to be rendered without the alpha test.
	StyleChoice style = ClassifyStyle(renderStyle, trans, HwStyleNoAlphaTest, fullbright, gameTexture->GetTranslucency(), -1);
	const int shaderIndex = gameTexture->GetShaderIndex();
	if ((particle->flags & SPF_ALLOWSHADERS) && m_materialProgramLookup && m_materialProgramLookup(shaderIndex) != nullptr)
	{
		style.Sorted = true;
		style.MaterialShader = shaderIndex;
	}

	// ---- orientation (the particle rules) ----
	const bool drawWithXYBillboard = (gl_billboard_particles || gl_billboard_mode == 1) && !(particle->flags & SPF_NO_XY_BILLBOARD);
	bool drawBillboardFacingCamera = gl_billboard_faces_camera;
	if (!hw_force_cambbpref && particle->texture.isValid())
	{
		if (particle->flags & SPF_NOFACECAMERA)
			drawBillboardFacingCamera = false;
		else if (particle->flags & SPF_FACECAMERA)
			drawBillboardFacingCamera = true;
	}
	EntityOrientation orientation;
	if (drawWithXYBillboard && drawBillboardFacingCamera) orientation = EntityOrientation::XYFaceCamera;
	else if (drawWithXYBillboard) orientation = EntityOrientation::XYBillboard;
	else if (drawBillboardFacingCamera) orientation = EntityOrientation::FaceCamera;
	else orientation = EntityOrientation::YBillboard;

	int options = style.Options;
	float roll = 0;
	if (spr->PT.flags & SPF_ROLL)
	{
		options |= EntityOptions::Roll;
		roll = spr->PT.Roll;
	}
	if (spr->PT.flags & SPF_ROLLCENTER)
		options |= EntityOptions::RollCenter;
	if (yflip)
		options |= EntityOptions::YFlip;
	if (fullbright || m_fullbrightState.Fullbright)
		options |= EntityOptions::FullbrightLight;
	if (level->flags3 & LEVEL3_NOCOLOREDSPRITELIGHTING)
		options |= EntityOptions::NoColoredLight;

	const SpriteLight light = GetSpriteLight(sector, nullptr, spr, particle->Pos, fullbright || m_fullbrightState.Fullbright, true);

	SpriteInstance instance;
	EntityVertex& vertex = instance.Vertex;
	vertex.PosX = (float)pos.X;
	vertex.PosY = (float)pos.Y;
	vertex.PosZ = (float)pos.Z;
	vertex.PrevPosX = (float)prevPos.X;
	vertex.PrevPosY = (float)prevPos.Y;
	vertex.PrevPosZ = (float)prevPos.Z;

	const int colorMapIndex = m_paletteColorMode ? (translation & 0x3FFF) : 0;
	vertex.SurfaceOptions = VertexOptions::EntityPackSurface(style.Alpha, style.Fuzz, flip ? 1 : 0, colorMapIndex, light.LightLevel);
	vertex.RenderOptions = VertexOptions::EntityPackRender(light.LightBufferIndex, renderIndex);
	vertex.TextureInfo = VertexOptions::EntityPackTextureInfo(texture->ArrayIndex, geometry.ImageWidth, geometry.ImageHeight);
	vertex.OffsetXYZ = VertexOptions::EntityPackXYZ(geometry.OffsetX, geometry.OffsetY - geometry.ImageHeight);

	vertex.AngleYaw = 0;
	vertex.AnglePitch = 0;
	vertex.AngleRoll = roll;
	vertex.ScaleX = (float)scaleX;
	vertex.ScaleY = (float)scaleY;
	vertex.SpriteOffsetX = (float)spr->GetOffset(false);
	vertex.SpriteOffsetY = (float)spr->GetOffset(true);
	vertex.TranslationIndex = (float)colorMapIndex;
	vertex.SpriteOptions = EntityVertex::PackSpriteOptions(orientation, options, style.FuzzType, 0);
	vertex.Alpha = style.Alpha;

	SetColors(vertex, light.RenderSector, thingColor, fullbright);
	SetLightProbe(vertex, nullptr, particle, fullbright);

	instance.Texture = texture;
	instance.Brightmap = brightmapTexture;
	instance.Style = style;
	instance.SortX = (float)pos.X;
	instance.SortY = (float)pos.Y;
	instance.SortZ = (float)(pos.Z + 0.5 * scaleY * geometry.ImageHeight);
	instance.ObjectColor = vertex.ObjectColor;
	instance.AddColor = vertex.AddColor;
	Emit(instance);
}

}
