/*
** EntityRenderer.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Entities.EntityRenderer (health bars are not ported, plan D20)
** with the CPU side of UZDoom's HWSprite::Process / PutSprite (hw_sprites.cpp, hw_setcolor.cpp style logic).
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

#include <algorithm>
#include <bit>
#include <cmath>
#include "r_defs.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityModelRenderer.h"
#include "Renderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Static/LightBufferType.h"
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

using namespace Helion::Geometry;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

namespace Enums = ::Helion::Resources::Definitions::Decorate::Properties::Enums;
using Data::RenderDataStyle;
using Helion::Util::Constants::LightBuffer;

EntityRenderer::EntityRenderer(Texture::Legacy::LegacyGLTextureManager& textureManager, EntityProgram* program, EntityTransparentProgram* programTransparent,
	EntityCompositeProgram* programComposite, EntityFuzzRefractionProgram* programFuzzRefraction)
	: m_textureManager(textureManager), m_program(program), m_programTransparent(programTransparent), m_programComposite(programComposite),
	  m_programFuzzRefraction(programFuzzRefraction)
{
	// Most things will be in the 5 texture array buckets. Anything that falls outside at runtime will end up in the overflow pool.
	// These come from sprites using colormaps in true color mode that need to be regenerated. (Blood colors, dehacked frames specifying a color map)
	m_renderDataPoolArray = std::make_unique<Data::RenderDataPool<EntityVertex>>(*m_program, 64, 8192);
	m_renderDataPoolOverflow = std::make_unique<Data::RenderDataPool<EntityVertex>>(*m_program, RenderPoolSize, 128);
	m_renderDataPoolSorted = std::make_unique<Data::RenderDataPool<EntityVertex>>(*m_program, 64, 1);

	m_dataManager = std::make_unique<Data::RenderDataManager<EntityVertex>>(*m_program, textureManager.BlackTexture, *m_renderDataPoolArray, *m_renderDataPoolOverflow);
	m_paletteColorMode = textureManager.PaletteColorMode();
}

EntityRenderer::~EntityRenderer()
{
	// The programs belong to HelionProgramRegistry; the data (VBOs) is released with the pools.
	m_dataManager->Dispose();
}

void EntityRenderer::SetRenderInfo(Shared::RenderInfo& renderInfo)
{
	m_renderInfo = &renderInfo;
	m_transferHeightView = renderInfo.TransferHeightView;
	m_spriteAlpha = true;
	m_spriteClip = gl_spriteclip;
	m_vanillaRender = gl_vanillarender;
	m_paletteColorMode = m_textureManager.PaletteColorMode();
	m_fullbrightState = GetFullbrightState(r_viewpoint.player);
}

void EntityRenderer::UpdateTo(FLevelLocals* world)
{
	m_level = world;
	m_renderDataPoolOverflow->RefillPool(RenderPoolSize / 4);
}

void EntityRenderer::Clear(FLevelLocals* world)
{
	m_level = world;
	m_dataManager->Clear();

	for (auto* data : m_sortedUsed)
	{
		data->Clear();
		m_renderDataPoolSorted->Return(data);
	}
	m_sortedUsed.clear();
	m_sortedItems.clear();

}

// ----------------------------------------------------------------------------------------------------------------
// Light / color helpers
// ----------------------------------------------------------------------------------------------------------------

float EntityRenderer::PackColor(PalEntry color)
{
	uint32_t packed = (uint32_t)color.r | ((uint32_t)color.g << 8) | ((uint32_t)color.b << 16) | ((uint32_t)color.a << 24);
	return std::bit_cast<float>(packed);
}

PalEntry EntityRenderer::UnpackColor(float packed)
{
	uint32_t bits = std::bit_cast<uint32_t>(packed);
	return PalEntry((bits >> 24) & 0xFF, bits & 0xFF, (bits >> 8) & 0xFF, (bits >> 16) & 0xFF);
}

// The light level of the vertex: the shader adds ExtraLight (view extralight) and the light buffer entry, so no RescaleLightLevel here.
EntityRenderer::SpriteLight EntityRenderer::GetSpriteLight(sector_t* sector, AActor* thing, DVisualThinker* spr, const DVector3& pos, bool fullbright, bool allowLightList)
{
	SpriteLight light;

	auto renderSector = ::Helion::World::Geometry::Sectors::GetRenderSector(sector, m_transferHeightView);
	sector_t* rs = renderSector.Sector != nullptr ? renderSector.Sector : sector;
	light.RenderSector = rs;

	int lightlevel;
	if (fullbright)
		lightlevel = 255;
	else if (thing != nullptr)
		lightlevel = thing->GetLightLevel(rs);
	else if (spr != nullptr)
		lightlevel = spr->GetLightLevel(rs);
	else
		lightlevel = rs->GetSpriteLight();

	if (thing != nullptr)
		lightlevel = rs->CheckSpriteGlow(lightlevel, pos);

	// The 3D light slab the sprite is in (Helion Entity.LightSector3D). HWSprite splits sprites at the slab boundaries; one
	// instance takes the light of the slab its center is in.
	sector_t* lightSector3D = nullptr;
	auto& lightlist = sector->e->XFloor.lightlist;
	if (lightlist.Size() != 0 && !fullbright && allowLightList)
	{
		const DVector2 xy(pos.X, pos.Y);
		for (unsigned i = 0; i < lightlist.Size(); i++)
		{
			double lightbottom = i < lightlist.Size() - 1 ? lightlist[i + 1].plane.ZatPoint(xy) : sector->floorplane.ZatPoint(xy);
			if (lightbottom < pos.Z)
			{
				lightlevel = *lightlist[i].p_lightlevel;
				if (thing != nullptr)
					lightlevel = rs->CheckSpriteGlow(lightlevel, pos);
				if (lightlist[i].caster != nullptr)
					lightSector3D = lightlist[i].caster->model;
				break;
			}
		}
	}

	light.LightLevel = std::clamp(lightlevel, 0, 255);
	light.LightSector3D = lightSector3D;

	using Geometry::Static::LightBufferType;
	if (lightSector3D != nullptr)
		light.LightBufferIndex = Renderer::GetLightBufferIndex(lightSector3D, LightBufferType::Wall);
	else
		light.LightBufferIndex = Renderer::GetLightBufferIndex(sector, sector->e->XFloor.ffloors.Size() > 0 ? LightBufferType::Wall : LightBufferType::Floor);
	return light;
}

void EntityRenderer::SetColors(EntityVertex& vertex, sector_t* renderSector, PalEntry thingColor, bool fullbright)
{
	thingColor.a = 255;
	if (renderSector != nullptr)
	{
		// Fullbright things keep their own color (DrawSprite).
		const bool ignoreSectorColor = fullbright;
		const PalEntry finalcol = ignoreSectorColor ? thingColor : thingColor.Modulate(renderSector->SpecialColors[sector_t::sprites]);
		vertex.ObjectColor = PackColor(finalcol);
		vertex.AddColor = PackColor(PalEntry(255, renderSector->AdditiveColors[sector_t::sprites].r, renderSector->AdditiveColors[sector_t::sprites].g,
			renderSector->AdditiveColors[sector_t::sprites].b));
	}
	else
	{
		vertex.ObjectColor = PackColor(thingColor);
		vertex.AddColor = PackColor(PalEntry(255, 0, 0, 0));
	}
}

void EntityRenderer::SetLightProbe(EntityVertex& vertex, AActor* actor, particle_t* particle, bool fullbright)
{
	vertex.LightProbeR = vertex.LightProbeG = vertex.LightProbeB = vertex.LightProbeW = 0;
	if (m_level == nullptr || m_fullbrightState.Fullbright || fullbright)
		return;

	const bool probes = m_level->LightProbes.Size() > 0;
	if (!probes && !m_level->HasDynamicLights)
		return;

	float out[3] = {};
	GetDynSpriteLight(m_level, gl_light_sprites ? actor : nullptr, gl_light_particles ? particle : nullptr, out);
	vertex.LightProbeR = out[0];
	vertex.LightProbeG = out[1];
	vertex.LightProbeB = out[2];
	vertex.LightProbeW = 1.0f;
}

// ----------------------------------------------------------------------------------------------------------------
// Texture geometry
// ----------------------------------------------------------------------------------------------------------------

EntityRenderer::TextureGeometry EntityRenderer::GetTextureGeometry(GLLegacyTexture* texture, bool flip)
{
	TextureGeometry geometry;
	const int imageW = texture->ImageDimension.Width > 0 ? texture->ImageDimension.Width : std::max(texture->Width(), 1);
	const int imageH = texture->ImageDimension.Height > 0 ? texture->ImageDimension.Height : std::max(texture->Height(), 1);

	geometry.ImageWidth = imageW;
	geometry.ImageHeight = imageH;
	if (texture->Source != nullptr)
	{
		// A texel is 1 / scale map units, like FGameTexture::SetSpriteRect.
		geometry.FoldX = 1.0f / texture->Source->GetScaleX();
		geometry.FoldY = 1.0f / texture->Source->GetScaleY();
	}
	else
	{
		geometry.FoldX = (float)std::max(texture->Width(), 1) / (float)imageW;
		geometry.FoldY = (float)std::max(texture->Height(), 1) / (float)imageH;
	}

	// Texture offsets are in image pixels. FGameTexture::SetSpriteRect positions hardware sprites with the r_spriteadjust
	// adjusted offsets (sprofs), not the plain texture offsets.
	const Vec2I offset = texture->Source != nullptr
		? Vec2I(texture->Source->GetTexelLeftOffset(r_spriteadjustHW), texture->Source->GetTexelTopOffset(r_spriteadjustHW))
		: texture->Offset();
	geometry.OffsetX = flip ? imageW - offset.X : offset.X;
	geometry.OffsetY = offset.Y;
	return geometry;
}

// ----------------------------------------------------------------------------------------------------------------
// Sprite clipping
// ----------------------------------------------------------------------------------------------------------------

// HWSprite::PerformSpriteClipAdjustment: returns the amount the sprite is moved along z (z1 = top, z2 = bottom of the unclipped sprite).
float EntityRenderer::GetSpriteClipShift(AActor* thing, const DVector2& thingpos, float spriteheight, float z1, float z2)
{
	const float NO_VAL = 100000000.0f;
	float shift = 0;
	bool clipthing = (thing->player || thing->flags3 & MF3_ISMONSTER || thing->IsKindOf(NAME_Inventory)) && (thing->flags & MF_ICECORPSE || !(thing->flags & MF_CORPSE));
	bool smarterclip = !clipthing && gl_spriteclip == 3;
	if ((clipthing || gl_spriteclip > 1) && !(thing->flags2 & MF2_FLOATBOB))
	{
		float btm = NO_VAL;
		float top = -NO_VAL;
		extsector_t::xfloor& x = thing->Sector->e->XFloor;

		if (x.ffloors.Size())
		{
			for (unsigned int i = 0; i < x.ffloors.Size(); i++)
			{
				F3DFloor* ff = x.ffloors[i];
				if (ff->flags & FF_THISINSIDE) continue;	// only relevant for software rendering.
				float floorh = ff->top.plane->ZatPoint(thingpos);
				float ceilingh = ff->bottom.plane->ZatPoint(thingpos);
				if (floorh == thing->floorz)
					btm = floorh;
				if (ceilingh == thing->ceilingz)
					top = ceilingh;
				if (btm != NO_VAL && top != -NO_VAL)
					break;
			}
		}
		else if (thing->Sector->GetHeightSec())
		{
			if (thing->flags2 & MF2_ONMOBJ && thing->floorz == thing->Sector->heightsec->floorplane.ZatPoint(thingpos))
			{
				btm = thing->floorz;
				top = thing->ceilingz;
			}
		}
		if (btm == NO_VAL)
			btm = thing->Sector->floorplane.ZatPoint(thing) - thing->Floorclip;
		if (top == NO_VAL)
			top = thing->Sector->ceilingplane.ZatPoint(thingpos);

		// +/-1 to account for the one pixel empty frame around the sprite.
		float diffb = (z2 + 1) - btm;
		float difft = (z1 - 1) - top;
		if (diffb >= 0) diffb = 0;
		// Adjust sprites clipping into ceiling and adjust clipping adjustment for tall graphics
		if (smarterclip)
		{
			// Reduce slightly clipping adjustment of corpses
			if (thing->flags & MF_CORPSE || spriteheight > fabs(diffb))
			{
				float ratio = clamp<float>((fabs(diffb) * (float)gl_sclipfactor / (spriteheight + 1)), 0.5, 1.0);
				diffb *= ratio;
			}
			if (!diffb)
			{
				if (difft <= 0) difft = 0;
				if (difft >= (float)gl_sclipthreshold)
				{
					// dumb copy of the above.
					if (!(thing->flags3 & MF3_ISMONSTER) || (thing->flags & MF_NOGRAVITY) || (thing->flags & MF_CORPSE) || difft > (float)gl_sclipthreshold)
						difft = 0;
				}
				if (spriteheight > fabs(difft))
				{
					float ratio = clamp<float>((fabs(difft) * (float)gl_sclipfactor / (spriteheight + 1)), 0.5, 1.0);
					difft *= ratio;
				}
				shift -= difft;
			}
		}
		if (diffb <= (0 - (float)gl_sclipthreshold))	// such a large displacement can't be correct!
		{
			// for living monsters standing on the floor allow a little more.
			if (!(thing->flags3 & MF3_ISMONSTER) || (thing->flags & MF_NOGRAVITY) || (thing->flags & MF_CORPSE) || diffb < (-1.8 * (float)gl_sclipthreshold))
				diffb = 0;
		}
		shift -= diffb;
	}
	return shift;
}

// The anamorphosis of HWSprite::Process: the sprite is scaled towards the viewpoint so that it does not clip into the floor / ceiling.
float EntityRenderer::GetAnamorphosisBias(AActor* thing, const DVector3& thingpos, float z1, float z2)
{
	const auto& vp = r_viewpoint;
	float minbias = r_spriteclipanamorphicminbias;
	minbias = clamp(minbias, 0.3f, 1.0f);

	float btm = thing->Sector->floorplane.ZatPoint(thing) - thing->Floorclip;
	float top = thing->Sector->ceilingplane.ZatPoint(thingpos);

	float vbtm = thing->Sector->floorplane.ZatPoint(vp.Pos);
	float vtop = thing->Sector->ceilingplane.ZatPoint(vp.Pos);

	float vpx = vp.Pos.X;
	float vpy = vp.Pos.Y;
	float vpz = vp.Pos.Z;

	float tpx = thingpos.X;
	float tpy = thingpos.Y;

	if (!(r_debug_nolimitanamorphoses))
	{
		// this should help prevent clipping through walls ...
		float objradiusbias = 1.f - thing->radius / sqrt((vpx - tpx) * (vpx - tpx) + (vpy - tpy) * (vpy - tpy));
		minbias = max(minbias, objradiusbias);
	}

	float bintersect, tintersect;
	if (z2 < vpz && vbtm < vpz)
		bintersect = min((btm - vpz) / (z2 - vpz), (vbtm - vpz) / (z2 - vpz));
	else
		bintersect = 1.0;

	if (z1 > vpz && vtop > vpz)
		tintersect = min((top - vpz) / (z1 - vpz), (vtop - vpz) / (z1 - vpz));
	else
		tintersect = 1.0;

	if (thing->waterlevel >= 1 && thing->waterlevel <= 2)
		bintersect = tintersect = 1.0f;

	return clamp(min(bintersect, tintersect), minbias, 1.0f);
}

// ----------------------------------------------------------------------------------------------------------------
// Style
// ----------------------------------------------------------------------------------------------------------------

// D10: Normal / Translucent / Add / ColorAdd / Fuzzy (alpha tested masked and the blended styles Helion has) go to the
// RenderDataManager buckets, every other style is drawn back to front by the SortedTranslucentRenderer.
EntityRenderer::StyleChoice EntityRenderer::ClassifyStyle(FRenderStyle rs, float trans, int hwStyleFlags, bool fullbright, bool translucentTexture, int fuzzOverride)
{
	StyleChoice choice;
	choice.Legacy = rs;
	choice.Alpha = trans;

	if (hwStyleFlags == HwStyleNoAlphaTest)
		choice.Options |= EntityOptions::NoAlphaTest;

	if (rs.Flags & STYLEF_RedIsAlpha)
		choice.Options |= EntityOptions::AlphaTexture;
	else if (rs.Flags & STYLEF_ColorIsFixed)
		choice.Options |= EntityOptions::Stencil;
	if (rs.Flags & STYLEF_InvertSource)
		choice.Options |= EntityOptions::InvertSource;
	if (rs.Flags & STYLEF_FadeToBlack)
		choice.Options |= EntityOptions::FadeToBlack;

	if (fuzzOverride >= 0)
	{
		// gl_fuzztype != 0: STYLE_Translucent drawn with the fuzz_*.fp material shader, trans 0.99, no alpha test. This is a plain
		// translucent draw (OIT transparent pass + composite): Helion's Fuzzy bucket only refracts (it never composites colour), so
		// only the shader variant (FuzzType) is set, not the Helion fuzz flag.
		choice.Style = Enums::RenderStyle::Translucent;
		choice.FuzzType = fuzzOverride;
		choice.Options |= EntityOptions::NoAlphaTest;
		return choice;
	}

	if (rs.BlendOp == STYLEOP_Shadow)
	{
		choice.Style = Enums::RenderStyle::Fuzzy;
		choice.Fuzz = 1;
		choice.FuzzType = 0;
		return choice;
	}

	if (rs.BlendOp == STYLEOP_Add)
	{
		const bool srcOne = rs.SrcAlpha == STYLEALPHA_One;
		const bool srcSrc = rs.SrcAlpha == STYLEALPHA_Src;
		const bool dstZero = rs.DestAlpha == STYLEALPHA_Zero;
		const bool dstInvSrc = rs.DestAlpha == STYLEALPHA_InvSrc;
		const bool dstOne = rs.DestAlpha == STYLEALPHA_One;

		if (srcOne && dstZero)
		{
			choice.Style = Enums::RenderStyle::Normal;
			return choice;
		}

		if (srcSrc && dstInvSrc)
		{
			// STYLE_Normal / STYLE_Translucent. A non translucent one stays in the opaque (alpha tested) pass.
			if (trans >= 1.0f - FLT_EPSILON && hwStyleFlags != HwStyleNoAlphaTest)
			{
				choice.Alpha = 1.0f;
				choice.Style = Enums::RenderStyle::Normal;
			}
			else
			{
				choice.Style = Enums::RenderStyle::Translucent;
			}
			return choice;
		}

		if ((srcSrc || srcOne) && dstOne)
		{
			// Optionally use STYLE_ColorBlend in place of STYLE_Add for fullbright items.
			if (srcSrc && trans > 1.0f - FLT_EPSILON && gl_usecolorblending && !m_fullbrightState.Fullbright && fullbright && !translucentTexture)
			{
				choice.Style = Enums::RenderStyle::ColorAdd;
				choice.Alpha = 1.0f;
			}
			else
			{
				choice.Style = Enums::RenderStyle::Add;
			}
			return choice;
		}

		if (rs.SrcAlpha == STYLEALPHA_SrcCol && dstOne)
		{
			// STYLE_ColorAdd
			choice.Style = Enums::RenderStyle::ColorAdd;
			choice.Alpha = 1.0f;
			return choice;
		}
	}

	// Subtract / reverse subtract, multiply, ... : drawn back to front after the OIT composite.
	choice.Sorted = true;
	if (rs.BlendOp == STYLEOP_Sub || rs.BlendOp == STYLEOP_RevSub)
		choice.Options |= EntityOptions::SubtractiveStyle;
	return choice;
}

// ----------------------------------------------------------------------------------------------------------------
// Emit
// ----------------------------------------------------------------------------------------------------------------

void EntityRenderer::Emit(const SpriteInstance& instance)
{
	GLLegacyTexture* texture = instance.Texture != nullptr ? instance.Texture : m_textureManager.NullTexture;
	GLLegacyTexture* arrayTexture = texture->ParentArrayTexture != nullptr ? texture->ParentArrayTexture : texture;

	if (instance.Style.Sorted && m_sortedRenderer != nullptr)
	{
		auto& data = m_renderDataPoolSorted->Get(arrayTexture, instance.Brightmap);
		data.ArrayData.Push(instance.Vertex);
		m_sortedUsed.push_back(&data);

		SortedItem item;
		item.Data = &data;
		item.X = instance.SortX;
		item.Y = instance.SortY;
		item.Z = instance.SortZ;
		item.Legacy = instance.Style.Legacy;
		item.MaterialShader = instance.Style.MaterialShader;
		item.ObjectColor = instance.ObjectColor;
		item.AddColor = instance.AddColor;
		item.LightProbe = FVector3(instance.Vertex.LightProbeR, instance.Vertex.LightProbeG, instance.Vertex.LightProbeB);
		m_sortedItems.push_back(item);
		return;
	}

	// Without a sorted renderer the style falls back to the closest OIT bucket.
	Enums::RenderStyle style = instance.Style.Sorted ? Enums::RenderStyle::Translucent : instance.Style.Style;
	auto& renderData = m_dataManager->GetByRenderStyle(style, arrayTexture, instance.Brightmap);
	renderData.ArrayData.Push(instance.Vertex);
}

void EntityRenderer::EmitParticle(EntityVertex& vertex, GLLegacyTexture* texture, GLLegacyTexture* brightmap, const StyleChoice& style, const DVector3& pos)
{
	SpriteInstance instance;
	instance.Vertex = vertex;
	instance.Texture = texture;
	instance.Brightmap = brightmap;
	instance.Style = style;
	instance.SortX = (float)pos.X;
	instance.SortY = (float)pos.Y;
	instance.SortZ = (float)pos.Z;
	instance.ObjectColor = vertex.ObjectColor;
	instance.AddColor = vertex.AddColor;
	Emit(instance);
}

// Queues the sorted draws of the tick; the closures run inside SortedTranslucentRenderer::Render. In BSP mode the renderer
// wants the emission index as key (the item emitted last is drawn first).
void EntityRenderer::QueueSortedDraws(Shared::RenderInfo& renderInfo)
{
	if (m_sortedRenderer == nullptr)
		return;

	const Vec3F& view = renderInfo.Camera->PositionInterpolated;
	const bool bspMode = m_sortedRenderer->BspMode();
	float index = 0;
	for (const SortedItem& item : m_sortedItems)
	{
		float key = bspMode ? index++ : Data::SortedTranslucentRenderer::DistanceSquared(item.X, item.Y, item.Z, view.X, view.Y, view.Z);
		EntityProgram* program = m_program;
		if (item.MaterialShader >= 0 && m_materialProgramLookup)
		{
			EntityProgram* material = m_materialProgramLookup(item.MaterialShader);
			if (material != nullptr)
				program = material;
		}

		m_sortedRenderer->Add(key, [this, item, program](Shared::RenderInfo& info)
		{
			program->Bind();
			SetUniforms(*program, info);
			if (item.MaterialShader >= 0 && program != m_program)
			{
				program->Tex(BindTextures::BoundTexture);
				program->TextureMode(Data::SortedTranslucentRenderer::GetTextureMode(item.Legacy));
				StreamData stream = {};
				stream.uObjectColor = UnpackColor(item.ObjectColor);
				stream.uObjectColor2 = UnpackColor(item.ObjectColor);
				stream.uAddColor = UnpackColor(item.AddColor);
				stream.uDynLightColor = FVector4(item.LightProbe.X, item.LightProbe.Y, item.LightProbe.Z, 1.0f);
				stream.timer = (float)(I_msTime() * 0.001);
				program->MaterialStreamData(stream);
			}
			else
			{
				program->FuzzTimer((float)(I_msTime() * 0.001));
			}

			Data::SortedTranslucentRenderer::SetStyle(*screen->RenderState(), item.Legacy);
			item.Data->Draw();
			program->Unbind();
		});
	}
}

// ----------------------------------------------------------------------------------------------------------------
// Actors
// ----------------------------------------------------------------------------------------------------------------

EntityOrientation EntityRenderer::GetActorOrientation(AActor* thing, uint32_t spritetype)
{
	if (spritetype == RF_FLATSPRITE)
		return EntityOrientation::Flat;
	if (spritetype == RF_WALLSPRITE)
		return EntityOrientation::Wall;

	const bool angledRoll = !!(thing->renderflags2 & RF2_ANGLEDROLL);
	const bool drawWithXYBillboard = !(thing->renderflags & RF_FORCEYBILLBOARD) && (gl_billboard_mode == 1 || (thing->renderflags & RF_FORCEXYBILLBOARD)) && !angledRoll;
	bool drawBillboardFacingCamera = gl_billboard_faces_camera;
	if (!hw_force_cambbpref)
	{
		if (thing->renderflags2 & RF2_BILLBOARDNOFACECAMERA)
			drawBillboardFacingCamera = false;
		else if (thing->renderflags2 & RF2_BILLBOARDFACECAMERA)
			drawBillboardFacingCamera = true;
	}

	if (drawWithXYBillboard && drawBillboardFacingCamera)
		return EntityOrientation::XYFaceCamera;
	if (drawWithXYBillboard)
		return EntityOrientation::XYBillboard;
	if (drawBillboardFacingCamera)
		return EntityOrientation::FaceCamera;
	return EntityOrientation::YBillboard;
}

void EntityRenderer::RenderEntity(AActor* thing, const Vec2D& position, int renderIndex)
{
	if (thing == nullptr)
		return;

	// [ZZ] allow CustomSprite-style direct picnum specification
	bool isPicnumOverride = thing->picnum.isValid();

	// Don't waste time projecting sprites that are definitely not visible.
	if ((thing->sprite == 0 && !isPicnumOverride) || !thing->IsVisibleToPlayer() || ((thing->renderflags & RF_MASKROTATION) && !thing->IsInsideVisibleAngles()))
		return;

	const auto& vp = r_viewpoint;
	AActor* camera = vp.camera;

	const double alpha = thing->Alpha;
	if (thing->renderflags & RF_INVISIBLE || !thing->RenderStyle.IsVisible(alpha))
	{
		if (!(thing->flags & MF_STEALTH) || !m_fullbrightState.StealthVision || thing == camera)
			return;
	}

	// check renderrequired vs ~r_rendercaps, if anything matches we don't support that feature,
	// check renderhidden vs r_rendercaps, if anything matches we do support that feature and should hide it.
	if ((!r_debug_disable_vis_filter && !!(thing->RenderRequired & ~r_renderercaps)) || (!!(thing->RenderHidden & r_renderercaps)))
		return;

	// Distance checks of classic RenderThings.
	FIntCVar* distanceCheck = thing->GetInfo()->distancecheck;
	DVector3 thingpos = thing->Pos();
	if (distanceCheck != nullptr && *distanceCheck >= 0)
	{
		double dist = (thingpos - (vp.bDoOob ? vp.OffPos : vp.Pos)).LengthSquared();
		double check = (double)**distanceCheck;
		if (dist >= check * check)
			return;
	}
	if (r_distance_cull_type > 0)
	{
		bool renderradcheck = thing->RenderRadius() < 0.25 * vp.culldist;
		double dist2 = renderradcheck ? (thingpos - (vp.bDoOob ? vp.OffPos : vp.Pos)).LengthSquared() :
			(thingpos - (vp.bDoOob ? vp.OffPos : vp.Pos)).Length() - 1.414 * thing->RenderRadius();
		if (dist2 >= (renderradcheck ? vp.culldistsq : vp.culldist))
			return;
	}

	AActor* viewmaster = thing;
	if ((thing->flags8 & MF8_MASTERNOSEE) && thing->master != nullptr)
		viewmaster = thing->master;

	// [Nash] filter visibility in mirrors
	if (thing->renderflags2 & RF2_INVISIBLEINMIRRORS && InMirror)
		return;
	else if (thing->renderflags2 & RF2_ONLYVISIBLEINMIRRORS && !InMirror)
		return;

	// Some added checks if the camera actor is not supposed to be seen.
	if (viewmaster == camera && !vp.showviewer)
	{
		if (vp.bForceNoViewer || (viewmaster->player && viewmaster->player->crossingPortal)) return;
		DVector3 vieworigin = viewmaster->Pos();
		if (fabs(vieworigin.X - vp.ActorPos.X) < 2 && fabs(vieworigin.Y - vp.ActorPos.Y) < 2) return;

		// Necessary in order to prevent sprite pop-ins with viewpos and models.
		auto* sec = viewmaster->Sector;
		if (sec && !sec->PortalBlocksMovement(sector_t::ceiling))
		{
			double zh = sec->GetPortalPlaneZ(sector_t::ceiling);
			double top = (viewmaster->player ? max<double>(viewmaster->player->viewz, viewmaster->Top()) + 1 : viewmaster->Top());
			if (viewmaster->Z() < zh && top >= zh)
				return;
		}
	}

	// Thing is invisible if close to the camera.
	if (viewmaster->renderflags & RF_MAYBEINVISIBLE)
	{
		DVector3 viewpos = viewmaster->Pos();
		if (fabs(viewpos.X - position.X) < 32 && fabs(viewpos.Y - position.Y) < 32) return;
	}

	// don't draw first frame of a player missile
	if (thing->flags & MF_MISSILE)
	{
		if (!(thing->flags7 & MF7_FLYCHEAT) && thing->target == vp.ViewActor && vp.ViewActor != nullptr)
		{
			double speed = thing->Vel.Length();
			if (speed >= thing->target->radius / 2)
			{
				double clipdist = clamp(thing->Speed, thing->target->radius, thing->target->radius * 2);
				if ((thingpos - vp.Pos).LengthSquared() < clipdist * clipdist) return;
			}
		}
		thing->flags7 |= MF7_FLYCHEAT;	// do this only once for the very first frame, but not if it gets into range again.
	}

	// Actors with a model frame: EntityModelRenderer.
	if (m_modelRenderer != nullptr && m_renderInfo != nullptr && m_modelRenderer->TryRenderActor(thing, position, *m_renderInfo, renderIndex))
		return;

	// Too close to the camera. This doesn't look good if it is a sprite.
	if (fabs(thingpos.X - vp.Pos.X) < 2 && fabs(thingpos.Y - vp.Pos.Y) < 2
		&& vp.Pos.Z >= thingpos.Z - 2 && vp.Pos.Z <= thingpos.Z + thing->Height + 2
		&& !thing->Vel.isZero())
	{
		return;
	}

	// [Nash] draw sprite shadow
	if (R_ShouldDrawSpriteShadow(thing))
	{
		double dist = (thingpos - vp.Pos).LengthSquared();
		double check = r_actorspriteshadowdist;
		if (dist <= check * check)
			ProcessSprite(thing, position, renderIndex, true);
	}

	ProcessSprite(thing, position, renderIndex, false);
}

void EntityRenderer::ProcessSprite(AActor* thing, const Vec2D& position, int renderIndex, bool isSpriteShadow)
{
	const auto& vp = r_viewpoint;
	FLevelLocals* level = thing->Level;
	m_level = level;

	bool isPicnumOverride = thing->picnum.isValid();

	int spritenum = thing->sprite;
	DVector2 sprscale = thing->Scale;
	if (thing->player != nullptr)
		P_CheckPlayerSprite(thing, spritenum, sprscale);

	const DVector3 thingpos = thing->Pos();
	const uint32_t spritetype = (thing->renderflags & RF_SPRITETYPEMASK);

	// ---- sprite frame ----
	bool mirror = false;
	DAngle ang = (thingpos.XY() - DVector2(position.X, position.Y)).Angle();
	if (vp.bDoOrtho)
	{
		ang = vp.Angles.Yaw;
	}
	else if (thing->renderflags2 & RF2_ANGLEDROLL)
	{
		Matrix3x4 rolltilt;
		rolltilt.MakeIdentity();
		DAngle tempang = thing->Angles.Yaw + thing->AngledRollOffset;
		rolltilt.Rotate(tempang.Cos(), tempang.Sin(), 0.0, -thing->Angles.Roll.Degrees());
		tempang = DVector3(rolltilt * FVector3(thingpos - vp.Pos)).Angle();
		bool backfaced = fabs((ang - tempang).Degrees()) > 90.0;
		if (backfaced)
			ang = DAngle::fromDeg(180.0) - ang;
	}

	FTextureID patch;
	if (isPicnumOverride)
	{
		// Animate picnum overrides.
		auto tex = TexMan.GetGameTexture(thing->picnum, true);
		if (tex == nullptr) return;

		if (tex->GetRotations() != 0xFFFF)
		{
			// choose a different rotation based on player view
			spriteframe_t* sprframe = &SpriteFrames[tex->GetRotations()];
			DAngle sprang = thing->GetSpriteAngle(ang, r_viewpoint.TicFrac);
			angle_t rot;
			if (sprframe->Texture[0] == sprframe->Texture[1])
			{
				if (thing->flags7 & MF7_SPRITEANGLE)
					rot = (thing->SpriteAngle + DAngle::fromDeg(45.0 / 2 * 9)).BAMs() >> 28;
				else
					rot = (sprang - (thing->Angles.Yaw + thing->SpriteRotation) + DAngle::fromDeg(45.0 / 2 * 9)).BAMs() >> 28;
			}
			else
			{
				if (thing->flags7 & MF7_SPRITEANGLE)
					rot = (thing->SpriteAngle + DAngle::fromDeg(45.0 / 2 * 9 - 180.0 / 16)).BAMs() >> 28;
				else
					rot = (sprang - (thing->Angles.Yaw + thing->SpriteRotation) + DAngle::fromDeg(45.0 / 2 * 9 - 180.0 / 16)).BAMs() >> 28;
			}
			if (sprframe->Flip & (1 << rot))
				mirror = true;
		}

		patch = tex->GetID();
	}
	else
	{
		DAngle sprangle;
		int rot;
		if (!(thing->renderflags & RF_FLATSPRITE) || thing->flags7 & MF7_SPRITEANGLE)
		{
			sprangle = thing->GetSpriteAngle(ang, r_viewpoint.TicFrac);
			rot = -1;
		}
		else
		{
			// Flat sprites cannot rotate in a predictable manner.
			sprangle = nullAngle;
			rot = 0;
		}
		patch = sprites[spritenum].GetSpriteFrame(thing->frame, rot, sprangle, &mirror, !!(thing->renderflags & RF_SPRITEFLIP));
	}

	if (!patch.isValid()) return;
	auto gameTexture = TexMan.GetGameTexture(patch, false);
	if (!gameTexture || !gameTexture->isValid()) return;

	// [SP] SpriteFlip
	const bool xflip = !!(thing->renderflags & RF_XFLIP) != !!(thing->renderflags & RF_SPRITEFLIP);
	const bool flip = mirror != xflip;
	bool yflip = !!(thing->renderflags & RF_YFLIP);
	// Canvas textures are stored upside down
	if (!isPicnumOverride && gameTexture->isHardwareCanvas())
		yflip = !yflip;

	const int translation = thing->Translation.index();
	const auto* spriteRotation = m_textureManager.GetSpriteRotation(patch, mirror, translation);
	if (spriteRotation == nullptr || spriteRotation->RenderStore == nullptr)
		return;

	GLLegacyTexture* texture = spriteRotation->RenderStore;
	GLLegacyTexture* brightmapTexture = spriteRotation->BrightmapRenderStore;

	// ---- position ----
	const DVector3 offsetWorld(thing->WorldOffset.X, thing->WorldOffset.Y, thing->WorldOffset.Z);
	DVector3 pos = thingpos + offsetWorld;
	DVector3 prevPos = thing->Prev + offsetWorld;
	if (spritetype == RF_FACESPRITE)
	{
		pos.Z -= thing->Floorclip;	// wall and flat sprites are to be considered level geometry so this may not apply.
		prevPos.Z -= thing->Floorclip;
	}

	if (isSpriteShadow)
	{
		// snap shadow Z to the floor
		pos.Z = prevPos.Z = thing->floorz;
	}
	else
	{
		// [RH] Make floatbobbing a renderer-only effect.
		pos.Z += thing->GetBobOffset(1.0);
		prevPos.Z += thing->GetBobOffset(0.0);
	}

	// No interpolation (RF_DONTINTERPOLATE) or the lag state of the network code freezes the object.
	if ((thing->renderflags & RF_DONTINTERPOLATE) || Net_ModifyObjectFrac(thing, 0.5) != 0.5)
		prevPos = pos;

	// ---- texture geometry ----
	const TextureGeometry geometry = GetTextureGeometry(texture, flip);
	double scaleX = sprscale.X * geometry.FoldX;
	double scaleY = sprscale.Y * geometry.FoldY;
	if (isSpriteShadow)
		scaleY *= 0.15;

	// Isometric sprites: vertical scale and partial rotation to compensate the Y billboarding (hw_sprites.cpp:1081-1135).
	float isoTheta = (float)vp.HWAngles.Pitch.Degrees();
	double isoScaleY = 1.0;
	bool isometric = false;
	if (thing->renderflags2 & RF2_ISOMETRICSPRITES)
	{
		isometric = true;
		const float rWidth = geometry.ImageWidth * geometry.FoldX;
		const float rHeight = geometry.ImageHeight * geometry.FoldY;
		float floordist = thing->radius * vp.floordistfact;
		floordist -= 0.5 * rWidth * vp.cotfloor;
		float sineisotheta = floordist / rHeight;
		double scl = g_sqrt(1.0 + sineisotheta * sineisotheta - 2.0 * vp.PitchSin * sineisotheta);
		if ((thing->radius > 0.0) && (scl > fabs(vp.PitchCos)))
		{
			isoScaleY = scl / (fabs(vp.PitchCos) > 0.01 ? fabs(vp.PitchCos) : 0.01);
			scaleY *= isoScaleY;
			isoTheta = (float)(180.0 * asin(sineisotheta / isoScaleY) / M_PI);
		}
		if (spritetype == RF_FACESPRITE)
		{
			// the face sprite is pushed back along the view vector by the actor radius
			const double shiftX = vp.ViewVector.X * thing->radius * 1.41421356237309504880;
			const double shiftY = vp.ViewVector.Y * thing->radius * 1.41421356237309504880;
			pos.X -= shiftX;
			pos.Y -= shiftY;
			prevPos.X -= shiftX;
			prevPos.Y -= shiftY;
		}
	}

	if (((thing->renderflags & RF_ROLLSPRITE) || (thing->renderflags2 & RF2_SQUAREPIXELS)) && !(thing->renderflags2 & RF2_STRETCHPIXELS))
	{
		double ps = level->pixelstretch;
		double mult = 1.0 / sqrt(ps); // shrink slightly
		scaleX *= mult * ps;
		scaleY *= mult;
	}

	const float offx = (float)thing->GetSpriteOffset(false);
	float offy = (float)thing->GetSpriteOffset(true);
	if (spritetype == RF_FLATSPRITE)
	{
		// [MC] flat sprites only move vertically by the sprite offset
		pos.Z += offy;
		prevPos.Z += offy;
	}

	// ---- sprite clip (UZDoom clip logic in place of Helion's GetOffsetZ heuristics) ----
	float offsetZUnits = (float)(geometry.OffsetY - geometry.ImageHeight);	// image pixels
	if (!m_vanillaRender && !isSpriteShadow && spritetype == RF_FACESPRITE)
	{
		// The rectangle of the original is the trimmed sprite rect (FGameTexture::GetSpritePositioning), in world units; the clip
		// tests use it, not the full picture. spriteheight is scaled twice there (r is already scaled).
		const FloatRect rect = gameTexture->GetSpritePositioning(1).GetSpriteRect();
		const double rectScaleY = scaleY / geometry.FoldY;
		const float spriteHeight = (float)(sprscale.Y * isoScaleY * rect.height * rectScaleY);
		float z1 = (float)(pos.Z - rect.top * rectScaleY);
		float z2 = (float)(z1 - rect.height * rectScaleY);
		if (spriteHeight > 0 && m_spriteClip > 0)
		{
			const float shift = GetSpriteClipShift(thing, thingpos.XY(), spriteHeight, z1, z2);
			pos.Z += shift;
			prevPos.Z += shift;
		}
		else if (m_spriteClip == -1)
		{
			// perform anamorphosis: the sprite (and its offsets) is scaled towards the viewpoint.
			const float bias = GetAnamorphosisBias(thing, thingpos, z1, z2);
			if (bias < 1.0f)
			{
				const float vpbias = 1.0f - bias;
				pos = DVector3(pos.X * bias + vp.Pos.X * vpbias, pos.Y * bias + vp.Pos.Y * vpbias, pos.Z * bias + vp.Pos.Z * vpbias);
				prevPos = DVector3(prevPos.X * bias + vp.Pos.X * vpbias, prevPos.Y * bias + vp.Pos.Y * vpbias, prevPos.Z * bias + vp.Pos.Z * vpbias);
				scaleX *= bias;
				scaleY *= bias;
			}
		}
	}

	// ---- light ----
	const bool brightmapNoFullbright = spriteRotation->BrightmapNoFullbright;
	const bool fullbright = (thing->flags5 & MF5_BRIGHT) || ((thing->renderflags & RF_FULLBRIGHT) && !gameTexture->isFullbrightDisabled() && !brightmapNoFullbright);

	// ---- style ----
	FRenderStyle renderStyle = thing->RenderStyle;
	PalEntry thingColor = (renderStyle.Flags & STYLEF_ColorIsFixed) ? PalEntry(thing->fillcolor) : PalEntry(255, 255, 255);
	thingColor.a = 255;

	if (m_fullbrightState.Fullbright && m_fullbrightState.Nightvision)
	{
		if ((thing->IsKindOf(NAME_Inventory) || thing->flags3 & MF3_ISMONSTER || thing->flags & MF_MISSILE || thing->flags & MF_CORPSE))
			renderStyle.Flags |= STYLEF_InvertSource;
	}

	int fuzzOverride = -1;
	float trans = (float)thing->Alpha;
	int hwStyleFlags = HwStyleNormal;

	if (renderStyle.BlendOp >= STYLEOP_Fuzz && renderStyle.BlendOp <= STYLEOP_FuzzOrRevSub)
	{
		renderStyle.CheckFuzz();
		if (renderStyle.BlendOp == STYLEOP_Fuzz)
		{
			if (gl_fuzztype != 0 && !(renderStyle.Flags & STYLEF_InvertSource))
			{
				renderStyle = LegacyRenderStyles[STYLE_Translucent];
				fuzzOverride = gl_fuzztype;
				trans = 0.99f;	// trans may not be 1 here
				hwStyleFlags = HwStyleNoAlphaTest;
			}
			else
			{
				renderStyle.BlendOp = STYLEOP_Shadow;
			}
		}
	}

	if (renderStyle.Flags & STYLEF_TransSoulsAlpha)
		trans = transsouls;
	else if (renderStyle.Flags & STYLEF_Alpha1)
		trans = 1.f;

	if (r_UseVanillaTransparency && (thing->renderflags & RF_ZDOOMTRANS))
	{
		// [SP] "canonical transparency" - with the flip of a CVar, disable transparency for Doom objects,
		//   and disable 'additive' translucency for certain objects from other games.
		if (r_UseVanillaTransparency == 1 || AutoTrans.CheckKey(thing->GetClass()->TypeName) != nullptr)
		{
			trans = 1.f;
			renderStyle.BlendOp = STYLEOP_Add;
			renderStyle.SrcAlpha = STYLEALPHA_One;
			renderStyle.DestAlpha = STYLEALPHA_Zero;
		}
	}

	const EntityOrientation orientation = GetActorOrientation(thing, spritetype);
	const bool faceCamera = orientation == EntityOrientation::FaceCamera || orientation == EntityOrientation::XYFaceCamera;

	if (trans >= 1.f - FLT_EPSILON && renderStyle.BlendOp != STYLEOP_Shadow && (
		(renderStyle.SrcAlpha == STYLEALPHA_One && renderStyle.DestAlpha == STYLEALPHA_Zero) ||
		(renderStyle.SrcAlpha == STYLEALPHA_Src && renderStyle.DestAlpha == STYLEALPHA_InvSrc)))
	{
		// This is a non-translucent sprite (i.e. STYLE_Normal or equivalent)
		trans = 1.f;

		if (!gl_sprite_blend || (thing->renderflags & (RF_FLATSPRITE | RF_WALLSPRITE)) || faceCamera)
		{
			renderStyle.SrcAlpha = STYLEALPHA_One;
			renderStyle.DestAlpha = STYLEALPHA_Zero;
			hwStyleFlags = HwStyleSolid;
		}
		else
		{
			renderStyle.SrcAlpha = STYLEALPHA_Src;
			renderStyle.DestAlpha = STYLEALPHA_InvSrc;
		}
	}

	const bool translucentTexture = gameTexture->GetTranslucency();
	if (translucentTexture || (renderStyle.Flags & STYLEF_RedIsAlpha))
	{
		if (hwStyleFlags == HwStyleSolid)
		{
			renderStyle.SrcAlpha = STYLEALPHA_Src;
			renderStyle.DestAlpha = STYLEALPHA_InvSrc;
		}
		hwStyleFlags = HwStyleNoAlphaTest;
	}

	if (m_fullbrightState.StealthVision && gl_enhanced_nightvision)
	{
		if (renderStyle.BlendOp == STYLEOP_Shadow)
		{
			// enhanced vision makes them more visible!
			trans = 0.5f;
			FRenderStyle rs = renderStyle;
			renderStyle = LegacyRenderStyles[STYLE_Translucent];
			renderStyle.Flags = rs.Flags;	// Flags must be preserved, at this point it can only be STYLEF_InvertSource
		}
		else if (thing->flags & MF_STEALTH)
		{
			// enhanced vision overcomes stealth!
			if (trans < 0.5f) trans = 0.5f;
		}
	}

	// for sprite shadow, use a translucent stencil renderstyle
	bool lightFullbright = fullbright;
	if (isSpriteShadow)
	{
		renderStyle = LegacyRenderStyles[STYLE_Stencil];
		thingColor = PalEntry(255, 0, 0, 0);
		fuzzOverride = -1;
		// fade shadow progressively as the thing moves higher away from the floor
		if (r_actorspriteshadowfadeheight > 0.0)
			trans *= clamp(0.0f, float(r_actorspriteshadowalpha - (thingpos.Z - thing->floorz) * (1.0 / r_actorspriteshadowfadeheight)), float(r_actorspriteshadowalpha));
		else
			trans *= r_actorspriteshadowalpha;
		hwStyleFlags = HwStyleNoAlphaTest;
		lightFullbright = false;
	}

	if (trans == 0.0f) return;

	if (!m_spriteAlpha && !isSpriteShadow)
	{
		renderStyle = LegacyRenderStyles[STYLE_Normal];
		trans = 1.f;
		hwStyleFlags = HwStyleNormal;
	}

	// the shadow is a translucent stencil
	if (isSpriteShadow)
	{
		renderStyle.SrcAlpha = STYLEALPHA_Src;
		renderStyle.DestAlpha = STYLEALPHA_InvSrc;
	}

	StyleChoice style = ClassifyStyle(renderStyle, trans, hwStyleFlags, lightFullbright, translucentTexture, fuzzOverride);

	// Sprites with a material shader (GLDEFS hardware shader / material, Warp, Specular, PBR) cannot go through the Helion array programs.
	const int shaderIndex = gameTexture->GetShaderIndex();
	if (!isSpriteShadow && m_materialProgramLookup && m_materialProgramLookup(shaderIndex) != nullptr)
	{
		style.Sorted = true;
		style.MaterialShader = shaderIndex;
	}

	// ---- instance ----
	int options = style.Options;
	const bool angledRoll = !!(thing->renderflags2 & RF2_ANGLEDROLL);
	if (yflip) options |= EntityOptions::YFlip;
	if (thing->renderflags & RF_ROLLSPRITE) options |= EntityOptions::Roll;
	if (thing->renderflags & RF_ROLLCENTER) options |= EntityOptions::RollCenter;
	if (angledRoll) options |= EntityOptions::AngledRoll;
	if (isometric && spritetype == RF_FACESPRITE) options |= EntityOptions::IsometricPitch;
	if (thing->renderflags & RF_ONESIDED) options |= EntityOptions::OneSided;
	if (lightFullbright) options |= EntityOptions::FullbrightLight;
	if (level->flags3 & LEVEL3_NOCOLOREDSPRITELIGHTING) options |= EntityOptions::NoColoredLight;

	const bool noLightList = lightFullbright || renderStyle.BlendOp == STYLEOP_Shadow || renderStyle.BlendOp == STYLEOP_RevSub || isSpriteShadow;
	const SpriteLight light = GetSpriteLight(thing->Sector, thing, nullptr, DVector3(thingpos.X, thingpos.Y, thing->Center()), lightFullbright || m_fullbrightState.Fullbright, !noLightList);

	const DRotator angles = (thing->renderflags & RF_INTERPOLATEANGLES) ? thing->InterpolatedAngles(r_viewpoint.TicFrac) : thing->Angles;
	double angleYaw = angles.Yaw.Degrees();
	if (angledRoll && spritetype == RF_FACESPRITE)
		angleYaw = (thing->Angles.Yaw + thing->AngledRollOffset).Degrees();

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
	vertex.OffsetXYZ = VertexOptions::EntityPackXYZ(geometry.OffsetX, (int)offsetZUnits);

	vertex.AngleYaw = (float)angleYaw;
	vertex.AnglePitch = (isometric && spritetype == RF_FACESPRITE) ? isoTheta : (float)angles.Pitch.Degrees();
	vertex.AngleRoll = (float)angles.Roll.Degrees();
	vertex.ScaleX = (float)scaleX;
	vertex.ScaleY = (float)scaleY;
	vertex.SpriteOffsetX = spritetype == RF_FLATSPRITE ? 0.0f : offx;
	vertex.SpriteOffsetY = spritetype == RF_FLATSPRITE ? 0.0f : offy;
	vertex.TranslationIndex = (float)colorMapIndex;
	vertex.SpriteOptions = EntityVertex::PackSpriteOptions(orientation, options, style.FuzzType, 0);
	vertex.Alpha = style.Alpha;

	SetColors(vertex, light.RenderSector, thingColor, lightFullbright);
	SetLightProbe(vertex, thing, nullptr, lightFullbright);

	instance.Texture = texture;
	instance.Brightmap = brightmapTexture;
	instance.Style = style;
	instance.SortX = (float)pos.X;
	instance.SortY = (float)pos.Y;
	instance.SortZ = (float)(pos.Z + 0.5 * scaleY * geometry.ImageHeight);
	instance.ObjectColor = vertex.ObjectColor;
	instance.AddColor = vertex.AddColor;
	// sprite shadows are always drawn behind the sprite they belong to
	if (isSpriteShadow)
		instance.SortZ += 0.001f;

	Emit(instance);
}

// ----------------------------------------------------------------------------------------------------------------
// Uniforms and passes
// ----------------------------------------------------------------------------------------------------------------

void EntityRenderer::SetUniforms(EntityProgram& program, Shared::RenderInfo& renderInfo)
{
	m_transferHeightView = renderInfo.TransferHeightView;
	program.BoundTexture(BindTextures::BoundTexture);
	program.BrightmapTexture(BindTextures::BrightmapTexture);
	program.ColormapTexture(BindTextures::Colormap);
	program.SectorColormapTexture(BindTextures::SectorColormap);
	program.SectorFogTexture(BindTextures::SectorFog);
	program.SectorGlowTexture(BindTextures::SectorGlow);
	program.ExtraLight(renderInfo.Uniforms.ExtraLightOrColorMapIndex);
	program.HasInvulnerability(renderInfo.Uniforms.DrawInvulnerability);
	program.LightLevelMix(renderInfo.Uniforms.Mix);
	program.Mvp(renderInfo.Uniforms.Mvp);
	program.MvpNoPitch(renderInfo.Uniforms.MvpNoPitch);
	program.FuzzFrac(renderInfo.Uniforms.TimeFrac);
	program.TimeFrac(renderInfo.TickFraction);

	const Vec3F& direction = renderInfo.Camera->Direction;
	float directionLength = sqrtf(direction.X * direction.X + direction.Y * direction.Y);
	if (directionLength > 0)
		program.ViewRightNormal(Vec2F(direction.Y / directionLength, -direction.X / directionLength));

	program.DistanceOffset(Renderer::GetDistanceOffset(renderInfo));
	program.ColorMix(renderInfo.Uniforms.ColorMix.Global);
	program.FuzzDiv(renderInfo.Uniforms.FuzzDiv);
	program.PaletteIndex((int)renderInfo.Uniforms.PaletteIndex);
	program.ColorMapIndex(renderInfo.Uniforms.ColorMapUniforms.GlobalIndex);
	program.LightMode(renderInfo.Uniforms.LightMode);
	program.GammaCorrection(renderInfo.Uniforms.GammaCorrection);
	program.ViewPos(renderInfo.Camera->Position);
	program.ScreenBounds(Vec2I(renderInfo.Viewport.width - 1, renderInfo.Viewport.height - 1));
	program.CheckPlaneClip(gl_vanillarender);
	program.UseBrightmaps(renderInfo.Uniforms.UseBrightmaps);
	program.UseSectorColor(renderInfo.Uniforms.SectorColor);
	program.UseSectorFog(renderInfo.Uniforms.SectorFogIndex);
	program.SetSpriteClipDownScaleAmount(std::max(renderInfo.Uniforms.DownScaleAmount, 1.0f));
	program.ColorClamp(1.0f);

	// The fade distance calculations work using squared distances
	float maxDistanceSquared = (float)renderInfo.Uniforms.MaxDistance * (float)renderInfo.Uniforms.MaxDistance;
	program.MaxDistanceSquared(maxDistanceSquared);
	program.FadeDistance(maxDistanceSquared / 2);

	if (dynamic_cast<EntityCompositeProgram*>(&program) != nullptr)
	{
		program.AccumTexture(BindTextures::AccumTexture);
		program.AccumCountTexture(BindTextures::AccumCountTexture);
	}

	if (dynamic_cast<EntityFuzzRefractionProgram*>(&program) != nullptr)
	{
		program.AccumTexture(BindTextures::AccumTexture);
		program.AccumCountTexture(BindTextures::AccumCountTexture);
		program.FuzzTexture(BindTextures::FuzzTexture);
		program.OpaqueTexture(BindTextures::OpaqueTexture);
	}

	program.WallClipTexture(BindTextures::WallClipTexture);
	program.PlaneClipTexture(BindTextures::PlaneClipTexture);
	program.MapDataTexture(BindTextures::MapLineData);
	program.LineHeightsTexture(BindTextures::LineHeights);

	// UZDoom: view angles for the billboard modes, level pixel stretch and the sprite CVars.
	const auto& vp = r_viewpoint;
	program.ViewAngles(Vec3F((float)vp.HWAngles.Yaw.Degrees(), (float)vp.HWAngles.Pitch.Degrees(), 0.0f));
	program.PixelStretch(m_level != nullptr ? (float)m_level->pixelstretch : 1.0f);
	program.UseLightProbe(gl_light_sprites);
	program.EnhancedNightvision(gl_enhanced_nightvision);
	program.MaskThreshold((float)gl_mask_sprite_threshold);
	if (m_level != nullptr)
	{
		const PalEntry cull = m_level->cullcolor;
		program.CullColor(FVector4(cull.r / 255.f, cull.g / 255.f, cull.b / 255.f, r_distance_cull_type > 1 ? 1.0f : 0.0f));
	}

	program.FuzzTimer((float)(I_msTime() * 0.001));
	program.OrthoView(vp.bDoOrtho);
	if (renderInfo.Viewpoint != nullptr)
		program.ViewpointUniforms(*renderInfo.Viewpoint);
}

void EntityRenderer::RenderOpaque(Shared::RenderInfo& renderInfo)
{
	m_program->Bind();
	SetUniforms(*m_program, renderInfo);
	m_dataManager->RenderByRenderStyle(RenderDataStyle::Normal);
}

void EntityRenderer::RenderOitTransparentPass(Shared::RenderInfo& renderInfo)
{
	m_programTransparent->Bind();
	m_programTransparent->RenderFuzz(false);
	SetUniforms(*m_programTransparent, renderInfo);
	m_programTransparent->MaskThreshold(0.0f);
	m_dataManager->RenderByRenderStyle(RenderDataStyle::Translucent);
	m_dataManager->RenderByRenderStyle(RenderDataStyle::Add);
	m_dataManager->RenderByRenderStyle(RenderDataStyle::Fuzzy);
	m_programTransparent->ColorClamp(0.9f);
	m_dataManager->RenderByRenderStyle(RenderDataStyle::ColorAdd);
	m_programTransparent->Unbind();
}

void EntityRenderer::RenderOitTransparentFuzzPass(Shared::RenderInfo& renderInfo)
{
	m_programTransparent->Bind();
	m_programTransparent->RenderFuzz(true);
	SetUniforms(*m_programTransparent, renderInfo);
	m_programTransparent->MaskThreshold(0.0f);
	m_dataManager->RenderByRenderStyle(RenderDataStyle::Fuzzy);
	m_programTransparent->Unbind();
}

void EntityRenderer::StartRenderOitCompositePass(Shared::RenderInfo& renderInfo)
{
	m_programComposite->Bind();
	SetUniforms(*m_programComposite, renderInfo);
}

void EntityRenderer::RenderOitFuzzRefractionPass(Shared::RenderInfo& renderInfo, bool renderColor)
{
	m_programFuzzRefraction->Bind();
	m_programFuzzRefraction->RenderFuzzRefractionColor(renderColor);
	SetUniforms(*m_programFuzzRefraction, renderInfo);
	m_programFuzzRefraction->MaskThreshold(0.0f);
	m_dataManager->RenderByRenderStyle(RenderDataStyle::Fuzzy);
	m_programFuzzRefraction->Unbind();
}

}
