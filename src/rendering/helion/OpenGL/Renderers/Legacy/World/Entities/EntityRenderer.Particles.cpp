/*
** EntityRenderer.Particles.cpp
**
** Particles of EntityRenderer: the CPU side of HWSprite::ProcessParticle (particle_t and the VisualThinkers that render
** as particles) and the particle gather of classic RenderParticles.
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

bool EntityRenderer::ParticleCulled(const DVector3& pos) const
{
	const auto& vp = r_viewpoint;
	if (r_distance_cull_type > 0)
	{
		double dist = (pos - (vp.bDoOob ? vp.OffPos : vp.Pos)).LengthSquared();
		if (dist >= vp.culldistsq)
			return true;
	}
	return false;
}

// Static render mode: no BSP walk, so every active particle / VisualThinker is culled by distance and the frustum.
void EntityRenderer::RenderParticles(FLevelLocals* level, Shared::RenderInfo& renderInfo, int renderIndex)
{
	m_level = level;
	m_renderInfo = &renderInfo;

	Common::Shared::FrustumPlanes planes;
	Common::Shared::Frustum::SetFrustumPlanes(renderInfo.Uniforms.Mvp, planes);

	for (DVisualThinker* sp = level->VisualThinkerHead; sp != nullptr; sp = sp->GetNext())
	{
		if (sp->ObjectFlags & OF_EuthanizeMe)
			continue;
		if (ParticleCulled(sp->PT.Pos))
			continue;

		double radius = sp->Scale.X;
		if (!(sp->flags & VTF_IsParticle))
		{
			auto tex = TexMan.GetGameTexture(sp->PT.texture, false);
			const double extent = tex != nullptr ? std::max(tex->GetDisplayWidth(), tex->GetDisplayHeight()) : 64.0;
			radius = extent * std::max(sp->Scale.X, sp->Scale.Y);
		}
		if (!planes.SphereInFrustum(Vec3D(sp->PT.Pos.X, sp->PT.Pos.Y, sp->PT.Pos.Z), radius))
			continue;

		subsector_t* sub = sp->PT.subsector != nullptr ? sp->PT.subsector : level->PointInRenderSubsector(sp->PT.Pos);
		RenderParticle(&sp->PT, sub->sector, sp, renderIndex);
	}

	if (!r_particles)
		return;

	for (uint16_t i = level->ActiveParticles; i != NO_PARTICLE; i = level->Particles[i].tnext)
	{
		particle_t* particle = &level->Particles[i];
		if (ParticleCulled(particle->Pos))
			continue;
		if (!planes.SphereInFrustum(Vec3D(particle->Pos.X, particle->Pos.Y, particle->Pos.Z), particle->size))
			continue;

		subsector_t* sub = particle->subsector != nullptr ? particle->subsector : level->PointInRenderSubsector(particle->Pos);
		RenderParticle(particle, sub->sector, nullptr, renderIndex);
	}
}

// BSP mode: classic RenderParticles(sub, front) for one visited subsector.
void EntityRenderer::RenderParticles(subsector_t* sub, Shared::RenderInfo& renderInfo, int renderIndex)
{
	m_renderInfo = &renderInfo;
	m_level = sub->sector->Level;
	RenderParticlesInSubsector(sub, renderIndex);
}

void EntityRenderer::RenderParticlesInSubsector(subsector_t* sub, int renderIndex)
{
	FLevelLocals* level = sub->sector->Level;
	for (uint32_t i = 0; i < sub->sprites.Size(); i++)
	{
		DVisualThinker* sp = sub->sprites[i];
		if (!sp || sp->ObjectFlags & OF_EuthanizeMe)
			continue;
		if (ParticleCulled(sp->PT.Pos))
			continue;
		RenderParticle(&sp->PT, sub->sector, sp, renderIndex);
	}

	for (int i = level->ParticlesInSubsec[sub->Index()]; i != NO_PARTICLE; i = level->Particles[i].snext)
	{
		if (ParticleCulled(level->Particles[i].Pos))
			continue;
		RenderParticle(&level->Particles[i], sub->sector, nullptr, renderIndex);
	}
}

// HWSprite::ProcessParticle
void EntityRenderer::RenderParticle(particle_t* particle, sector_t* sector, DVisualThinker* spr, int renderIndex)
{
	if (!particle || particle->alpha <= 0)
		return;

	if (spr && !spr->ValidTexture())
		return;

	if (spr && !(spr->flags & VTF_IsParticle))
	{
		RenderVisualThinker(spr, sector, renderIndex);
		return;
	}

	const auto& vp = r_viewpoint;
	FLevelLocals* level = sector->Level;
	m_level = level;

	const bool fullbright = !!(particle->flags & SPF_FULLBRIGHT);
	float trans = particle->alpha;

	FRenderStyle renderStyle = particle->style != STYLE_None ? LegacyRenderStyles[particle->style] : LegacyRenderStyles[STYLE_Translucent];
	if (renderStyle.BlendOp >= STYLEOP_Fuzz && renderStyle.BlendOp <= STYLEOP_FuzzOrRevSub)
	{
		renderStyle.CheckFuzz();
		if (renderStyle.BlendOp == STYLEOP_Fuzz)
			renderStyle.BlendOp = STYLEOP_Shadow;
	}

	PalEntry thingColor(particle->color);
	thingColor.a = 255;

	// ---- shape ----
	bool hasTexture = false;
	bool customAnimatedTexture = false;
	int particleStyle = 0;
	float size = particle->size;
	if (!spr)
	{
		hasTexture = particle->texture.isValid();
		customAnimatedTexture = (particle->flags & SPF_LOCAL_ANIM) && particle->animData.ok;
		particleStyle = hasTexture ? 2 : gl_particles_style; // Treat custom texture the same as smooth particles
	}
	else
	{
		size = float(spr->Scale.X);
		const int ptype = spr->GetParticleType();
		particleStyle = (ptype != PT_DEFAULT) ? ptype : gl_particles_style;
	}

	double timefrac = Net_ModifyParticleFrac(particle, vp.TicFrac);
	if (paused || (level->isFrozen() && !(particle->flags & SPF_NOTIMEFREEZE)))
		timefrac = 0.;

	GLLegacyTexture* texture = m_textureManager.NullTexture;
	GLLegacyTexture* brightmapTexture = nullptr;
	bool procedural = true;
	if (hasTexture)
	{
		FTextureID lump = customAnimatedTexture ? TexAnim.UpdateStandaloneAnimation(particle->animData, level->LocalWorldTimer + timefrac) : particle->texture;
		auto gameTexture = lump.isValid() ? TexMan.GetGameTexture(lump, !customAnimatedTexture) : nullptr;
		if (gameTexture != nullptr && gameTexture->isValid())
		{
			const auto* rotation = m_textureManager.GetSpriteRotation(lump, false, 0);
			if (rotation != nullptr && rotation->RenderStore != nullptr)
			{
				texture = rotation->RenderStore;
				brightmapTexture = rotation->BrightmapRenderStore;
				procedural = false;
			}
		}
	}

	float factor;
	if (particleStyle == 1) factor = 1.3f / 7.f;
	else if (particleStyle == 2) factor = 2.5f / 7.f;
	else factor = 1 / 7.f;
	float scalefac = size * factor;
	const float ps = level->pixelstretch;
	scalefac /= sqrt(ps); // shrink it slightly to account for the stretch
	// The original's horizontal half extent is (ViewVector.Y * scalefac, -ViewVector.X * scalefac * ps): only the view's X component
	// is stretched, so the width depends on the view direction.
	const float widthfac = (float)sqrt(vp.ViewVector.Y * vp.ViewVector.Y + vp.ViewVector.X * vp.ViewVector.X * ps * ps);

	// ---- style ----
	// [BB] Translucent particles have to be rendered without the alpha test.
	int hwStyleFlags;
	if (particleStyle != 2 && trans >= 1.0f - FLT_EPSILON)
	{
		hwStyleFlags = HwStyleSolid;
		if (renderStyle.SrcAlpha == STYLEALPHA_Src && renderStyle.DestAlpha == STYLEALPHA_InvSrc && renderStyle.BlendOp == STYLEOP_Add)
		{
			renderStyle.SrcAlpha = STYLEALPHA_One;
			renderStyle.DestAlpha = STYLEALPHA_Zero;
		}
	}
	else
	{
		hwStyleFlags = HwStyleNoAlphaTest;
	}

	if (!m_spriteAlpha)
	{
		renderStyle = LegacyRenderStyles[STYLE_Normal];
		trans = 1.f;
	}

	StyleChoice style = ClassifyStyle(renderStyle, trans, hwStyleFlags, fullbright, false, -1);

	// ---- orientation ----
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
	if (particle->flags & SPF_ROLL)
	{
		options |= EntityOptions::Roll;
		roll = particle->Roll;
	}
	if (particle->flags & SPF_ROLLCENTER)
		options |= EntityOptions::RollCenter;
	if (fullbright || m_fullbrightState.Fullbright)
		options |= EntityOptions::FullbrightLight;
	if (level->flags3 & LEVEL3_NOCOLOREDSPRITELIGHTING)
		options |= EntityOptions::NoColoredLight;
	if (procedural)
		options |= EntityOptions::Procedural;

	// ---- position: the particle moves by its velocity during the tick, interpolated by the shader ----
	DVector3 prevPos = particle->Pos;
	DVector3 pos = particle->Pos;
	if (timefrac != 0.)
	{
		pos.X += particle->Vel.X;
		pos.Y += particle->Vel.Y;
		pos.Z += particle->Vel.Z;
	}

	// ---- light ----
	const SpriteLight light = GetSpriteLight(sector, nullptr, spr, particle->Pos, fullbright || m_fullbrightState.Fullbright, true);

	SpriteInstance instance;
	EntityVertex& vertex = instance.Vertex;
	vertex.PosX = (float)pos.X;
	vertex.PosY = (float)pos.Y;
	vertex.PosZ = (float)pos.Z;
	vertex.PrevPosX = (float)prevPos.X;
	vertex.PrevPosY = (float)prevPos.Y;
	vertex.PrevPosZ = (float)prevPos.Z;

	const int imageW = texture->ImageDimension.Width > 0 ? texture->ImageDimension.Width : std::max(texture->Width(), 1);
	const int imageH = texture->ImageDimension.Height > 0 ? texture->ImageDimension.Height : std::max(texture->Height(), 1);
	vertex.SurfaceOptions = VertexOptions::EntityPackSurface(style.Alpha, style.Fuzz, 0, 0, light.LightLevel);
	vertex.RenderOptions = VertexOptions::EntityPackRender(light.LightBufferIndex, renderIndex);
	vertex.TextureInfo = VertexOptions::EntityPackTextureInfo(texture->ArrayIndex, imageW, imageH);

	if (procedural)
	{
		// Pos is the center, Scale the half extent in world units.
		vertex.OffsetXYZ = VertexOptions::EntityPackXYZ(0, 0);
		vertex.ScaleX = scalefac * widthfac;
		vertex.ScaleY = scalefac;
	}
	else
	{
		// The custom texture covers the whole quad of half extent scalefac, centered on Pos.
		vertex.OffsetXYZ = VertexOptions::EntityPackXYZ(imageW / 2, -(imageH / 2));
		vertex.ScaleX = 2.0f * scalefac * widthfac / (float)imageW;
		vertex.ScaleY = 2.0f * scalefac / (float)imageH;
	}

	vertex.AngleYaw = 0;
	vertex.AnglePitch = 0;
	vertex.AngleRoll = roll;
	vertex.SpriteOffsetX = 0;
	vertex.SpriteOffsetY = 0;
	vertex.TranslationIndex = 0;
	const int shape = procedural ? std::clamp(particleStyle, 0, 2) : 0;
	vertex.SpriteOptions = EntityVertex::PackSpriteOptions(orientation, options, style.FuzzType, shape);
	vertex.Alpha = style.Alpha;

	SetColors(vertex, light.RenderSector, thingColor, fullbright);
	SetLightProbe(vertex, nullptr, particle, fullbright);

	EmitParticle(vertex, texture, brightmapTexture, style, pos);
}

}
