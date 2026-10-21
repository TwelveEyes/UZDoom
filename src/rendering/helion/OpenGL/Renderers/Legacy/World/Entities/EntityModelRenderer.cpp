/*
** EntityModelRenderer.cpp
**
** UZDoom-only (plan M4.3): see EntityModelRenderer.h.
**
** Sources: hw_models.cpp (FHWModelRenderer), hw_sprites.cpp (HWSprite::Process / PutSprite / DrawSprite for models),
** hw_spritelight.cpp (hw_GetDynModelLight), hw_weapon.cpp (DrawPSprite for models).
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

#include <algorithm>
#include <cfloat>
#include <cmath>
#include "EntityModelRenderer.h"
#include "BindTextures.h"
#include "OpenGL/Shader/TextureUnits.h"
#include "Renderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Static/LightBufferType.h"
#include "hw_lightbuffer.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ModelLightList.h"
#include "World/Geometry/Sectors/RenderSector.h"
#include "a_dynlight.h"
#include "actor.h"
#include "actorinlines.h"
#include "d_net.h"
#include "d_player.h"
#include "engineerrors.h"
#include "g_levellocals.h"
#include "gametexture.h"
#include "hw_bonebuffer.h"
#include "hw_cvars.h"
#include "hw_material.h"
#include "hw_modelvertexbuffer.h"
#include "i_interface.h"
#include "i_time.h"
#include "models.h"
#include "p_effect.h"
#include "r_data/r_vanillatrans.h"
#include "r_utility.h"
#include "texturemanager.h"
#include "v_video.h"
#include "vm.h"

EXTERN_CVAR(Float, transsouls)
EXTERN_CVAR(Int, r_extralight)
EXTERN_CVAR(Int, gl_fuzztype)
EXTERN_CVAR(Bool, gl_enhanced_nightvision)
EXTERN_CVAR(Bool, gl_light_sprites)
EXTERN_CVAR(Bool, gl_light_models)
EXTERN_CVAR(Float, gl_mask_sprite_threshold)

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

using ::Helion::Geometry::Vec2D;
using ::Helion::Render::OpenGL::Shader::TextureUnits;
using Data::SortedTranslucentRenderer;

namespace
{

// The hw_lighting.h RescaleLightLevel.
int RescaleLightLevel(int lightlevel)
{
	int extralight = r_extralight;
	// max is needed for negative r_extralight values
	return std::max(std::clamp(lightlevel, 0, 255) * (255 - extralight) / 255 + extralight, 0);
}

// The layer textures of a material beyond the base texture bind to these units (texture2 .. texture12 of the contract).
constexpr int FirstLayerUnit = (int)BindTextures::MaterialLayer1;

// The bone and light buffers mapped for the duration of a draw (hw_drawinfo.cpp maps them around the draws, uploads
// happen while drawing).
class BufferMapScope
{
public:
	BufferMapScope()
	{
		screen->mBones->Map();
		screen->mLights->Map();
	}

	~BufferMapScope()
	{
		screen->mLights->Unmap();
		screen->mBones->Unmap();
	}

	BufferMapScope(const BufferMapScope&) = delete;
	BufferMapScope& operator=(const BufferMapScope&) = delete;
};

}

EntityModelRenderer::EntityModelRenderer(ProgramLookup programLookup) : m_programLookup(std::move(programLookup))
{
	m_stream = {};
}

EntityModelRenderer::~EntityModelRenderer() = default;

void EntityModelRenderer::Clear()
{
	m_actors.clear();
	m_opaque.clear();
	m_fullbrightValid = false;
}

// ----------------------------------------------------------------------------------------------------------------
// Gather: HWSprite::Process for the model, PutSprite
// ----------------------------------------------------------------------------------------------------------------

FSpriteModelFrame* EntityModelRenderer::FindActorModelFrame(AActor* thing)
{
	// [ZZ] a direct picnum specification draws the sprite
	if (thing == nullptr || thing->picnum.isValid())
		return nullptr;

	int spritenum = thing->sprite;
	DVector2 sprscale = thing->InterpolatedScale(r_viewpoint.TicFrac);
	if (thing->player != nullptr)
		P_CheckPlayerSprite(thing, spritenum, sprscale);

	return FindModelFrame(thing, spritenum, thing->frame, !!(thing->flags & MF_DROPPED));
}

bool EntityModelRenderer::TryRenderActor(AActor* thing, const Vec2D& viewPosition, Shared::RenderInfo& renderInfo, int renderIndex)
{
	(void)viewPosition;
	(void)renderInfo;
	(void)renderIndex;

	if (FindActorModelFrame(thing) == nullptr)
		return false;

	// The actors are gathered once per tick like the sprites; their draws (interpolated position, frame, light, sorted
	// translucent draw) are built every frame by QueueFrame.
	m_actors.push_back(thing);
	return true;
}

void EntityModelRenderer::QueueFrame(Shared::RenderInfo& renderInfo)
{
	m_opaque.clear();
	m_fullbrightValid = false;
	for (AActor* thing : m_actors)
		ProcessActor(thing, renderInfo);
}

void EntityModelRenderer::ProcessActor(AActor* thing, Shared::RenderInfo& renderInfo)
{
	FSpriteModelFrame* smf = FindActorModelFrame(thing);
	if (smf == nullptr)
		return;

	if (!m_fullbrightValid)
	{
		m_fullbrightState = GetFullbrightState(r_viewpoint.player);
		m_fullbrightValid = true;
	}

	const auto& vp = r_viewpoint;
	const double ticFrac = vp.TicFrac;
	FLevelLocals* level = thing->Level;
	const int smfFlags = smf->getFlags(thing->modelData);

	// [RH] Interpolate the sprite's position to make it look smooth
	const DVector3 thingpos = thing->InterpolatedPosition(ticFrac);

	ModelDraw draw;
	draw.Actor = thing;
	draw.Frame = smf;
	draw.TicFrac = Net_ModifyObjectFrac(thing, ticFrac);
	draw.X = (float)(thingpos.X + thing->WorldOffset.X);
	draw.Y = (float)(thingpos.Y + thing->WorldOffset.Y);
	double z = thingpos.Z + thing->WorldOffset.Z;
	if ((thing->renderflags & RF_SPRITETYPEMASK) == RF_FACESPRITE)
		z -= thing->Floorclip; // wall and flat sprites are to be considered level geometry so this may not apply.
	// [RH] Make floatbobbing a renderer-only effect.
	z += thing->GetBobOffset(draw.TicFrac);
	draw.Z = (float)z;

	// ---- light calculation (hw_sprites.cpp) ----
	auto renderSector = ::Helion::World::Geometry::Sectors::GetRenderSector(thing->Sector, renderInfo.TransferHeightView);
	sector_t* rs = renderSector.Sector != nullptr ? renderSector.Sector : thing->Sector;

	// allow disabling of the fullbright flag by a brightmap definition
	// (e.g. to do the gun flashes of Doom's zombies correctly.
	const bool fullbright = (thing->flags5 & MF5_BRIGHT) || (thing->renderflags & RF_FULLBRIGHT);
	const bool fullbrightScene = m_fullbrightState.Fullbright;

	int lightlevel = fullbright ? 255 : RescaleLightLevel(thing->GetLightLevel(rs));
	lightlevel = rs->CheckSpriteGlow(lightlevel, thingpos);

	// The sprite is lit by the 3D light slab its center is in (HWSprite splits the sprite at the slab boundaries with clip planes).
	sector_t* lightSector3D = nullptr;
	auto& lightlist = thing->Sector->e->XFloor.lightlist;
	if (lightlist.Size() != 0 && !fullbrightScene && !fullbright)
	{
		const double lightZ = thingpos.Z + thing->Height * 0.5;
		DVector2 xy(thingpos.X, thingpos.Y);
		for (unsigned i = 0; i < lightlist.Size(); i++)
		{
			double lightbottom;
			if (i < lightlist.Size() - 1)
				lightbottom = lightlist[i + 1].plane.ZatPoint(xy);
			else
				lightbottom = thing->Sector->floorplane.ZatPoint(xy);

			if (lightbottom < lightZ)
			{
				lightlevel = RescaleLightLevel(*lightlist[i].p_lightlevel);
				lightlevel = (uint8_t)thing->Sector->CheckSpriteGlow(lightlevel, thingpos);
				if (lightlist[i].caster != nullptr)
					lightSector3D = lightlist[i].caster->model;
				break;
			}
		}
	}

	using Geometry::Static::LightBufferType;
	if (lightSector3D != nullptr)
		draw.LightBufferIndex = Renderer::GetLightBufferIndex(lightSector3D, LightBufferType::Wall);
	else
		draw.LightBufferIndex = Renderer::GetLightBufferIndex(thing->Sector, thing->Sector->e->XFloor.ffloors.Size() > 0 ? LightBufferType::Wall : LightBufferType::Floor);

	PalEntry thingColor = (thing->RenderStyle.Flags & STYLEF_ColorIsFixed) ? PalEntry(thing->fillcolor) : PalEntry(0xffffffff);
	thingColor.a = 255;
	FRenderStyle renderStyle = thing->RenderStyle;
	bool enhancedvision = false;

	if (fullbrightScene)
	{
		enhancedvision = m_fullbrightState.StealthVision;
		draw.Desaturation = 0;

		if (m_fullbrightState.Nightvision)
		{
			if ((thing->IsKindOf(NAME_Inventory) || thing->flags3 & MF3_ISMONSTER || thing->flags & MF_MISSILE || thing->flags & MF_CORPSE))
				renderStyle.Flags |= STYLEF_InvertSource;
		}
	}
	else
	{
		// Only the white light of a fullbright object is applied by the Fullbright option, fade and desaturation of the colormap stay.
		draw.Desaturation = rs->Colormap.Desaturation;
		if (!fullbright && (level->flags3 & LEVEL3_NOCOLOREDSPRITELIGHTING))
			draw.Options |= ModelOptions::NoColoredLight;
	}
	if (fullbright || fullbrightScene)
		draw.Options |= ModelOptions::Fullbright;

	// ---- render style ----
	float trans = (float)thing->InterpolatedAlpha(ticFrac);
	bool noAlphaTest = false;
	bool solid = false;

	if (renderStyle.BlendOp >= STYLEOP_Fuzz && renderStyle.BlendOp <= STYLEOP_FuzzOrRevSub)
	{
		renderStyle.CheckFuzz();
		if (renderStyle.BlendOp == STYLEOP_Fuzz)
		{
			if (gl_fuzztype != 0 && !(renderStyle.Flags & STYLEF_InvertSource))
			{
				// The fuzz_*.fp shader override does not apply to models (FHWModelRenderer::SetMaterial always passes -1): a plain translucent model.
				renderStyle = LegacyRenderStyles[STYLE_Translucent];
				trans = 0.99f;	// trans may not be 1 here
				noAlphaTest = true;
			}
			else
			{
				// Without shaders only the standard effect is available.
				renderStyle.BlendOp = STYLEOP_Shadow;
			}
		}
	}

	if (renderStyle.Flags & STYLEF_TransSoulsAlpha)
	{
		trans = transsouls;
	}
	else if (renderStyle.Flags & STYLEF_Alpha1)
	{
		trans = 1.f;
	}
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
	if (trans >= 1.f - FLT_EPSILON && renderStyle.BlendOp != STYLEOP_Shadow && (
		(renderStyle.SrcAlpha == STYLEALPHA_One && renderStyle.DestAlpha == STYLEALPHA_Zero) ||
		(renderStyle.SrcAlpha == STYLEALPHA_Src && renderStyle.DestAlpha == STYLEALPHA_InvSrc)
		))
	{
		// This is a non-translucent sprite (i.e. STYLE_Normal or equivalent). A model is always solid then.
		trans = 1.f;
		renderStyle.SrcAlpha = STYLEALPHA_One;
		renderStyle.DestAlpha = STYLEALPHA_Zero;
		solid = true;
	}
	if ((renderStyle.Flags & STYLEF_RedIsAlpha) || thing->RenderStyle != DefaultRenderStyle())
	{
		if (solid)
		{
			renderStyle.SrcAlpha = STYLEALPHA_Src;
			renderStyle.DestAlpha = STYLEALPHA_InvSrc;
		}
		solid = false;
		noAlphaTest = true;
	}

	if (enhancedvision && gl_enhanced_nightvision)
	{
		if (renderStyle.BlendOp == STYLEOP_Shadow)
		{
			// enhanced vision makes them more visible!
			trans = 0.5f;
			FRenderStyle rs2 = renderStyle;
			renderStyle = STYLE_Translucent;
			renderStyle.Flags = rs2.Flags;	// Flags must be preserved, at this point it can only be STYLEF_InvertSource
		}
		else if (thing->flags & MF_STEALTH)
		{
			// enhanced vision overcomes stealth!
			if (trans < 0.5f) trans = 0.5f;
		}
	}

	if (trans == 0.0f)
		return;

	draw.Translucent = !solid;
	draw.NoAlphaTest = noAlphaTest;

	// ---- DrawSprite for the translucent pass ----
	bool additiveFog = false;
	if (draw.Translucent)
	{
		// for special render styles brightmaps would not look good - especially for subtractive.
		draw.Brightmap = renderStyle.BlendOp == STYLEOP_Add;

		if (renderStyle.BlendOp == STYLEOP_Shadow)
		{
			// fog + fuzz don't work well without some fiddling with the alpha value (the fog factor of HWSprite::DrawSprite is not applied).
			trans = 0.44f;
			additiveFog = true;
		}
		else if (renderStyle.BlendOp == STYLEOP_Add && renderStyle.DestAlpha == STYLEALPHA_One)
		{
			additiveFog = true;
		}
	}
	if (renderStyle.Flags & STYLEF_FadeToBlack)
		additiveFog = true;
	if (renderStyle.BlendOp == STYLEOP_RevSub || renderStyle.BlendOp == STYLEOP_Sub)
		renderStyle.BlendOp = STYLEOP_Fuzz;	// subtractive with models is not going to work.
	if (additiveFog)
		draw.Options |= ModelOptions::BlackFog;

	draw.RenderStyle = renderStyle;
	draw.Alpha = trans;
	draw.LightLevel = lightlevel;

	if (renderStyle.BlendOp == STYLEOP_Shadow)
	{
		// state.SetColor(0.2f, 0.2f, 0.2f, fuzzalpha): the light is not applied.
		draw.ObjectColor = PalEntry(255, 51, 51, 51);
		draw.AddColor = 0;
		draw.Options |= ModelOptions::Fullbright;
	}
	else
	{
		sector_t* cursec = thing->Sector;
		const PalEntry finalcol = fullbright ? thingColor : thingColor.Modulate(cursec->SpecialColors[sector_t::sprites]);
		draw.ObjectColor = finalcol;
		draw.AddColor = cursec->AdditiveColors[sector_t::sprites] | 0xff000000;

		if (level->HasDynamicLights && !fullbrightScene && !fullbright)
		{
			// That's a lot of checks...
			if (!smf->isVoxel && !(smfFlags & MDL_NOPERPIXELLIGHTING) && gl_light_models && gl_light_sprites && !(thing->renderflags2 & RF2_NODYNAMICLIGHTING))
			{
				draw.PerPixelLights = true;
			}
			else
			{
				float out[3] = {};
				GetDynSpriteLight(level, gl_light_sprites ? thing : nullptr, nullptr, out);
				draw.DynLight[0] = out[0];
				draw.DynLight[1] = out[1];
				draw.DynLight[2] = out[2];
				draw.HasDynLight = true;
			}
		}
	}

	if (level->LightProbes.Size() > 0)
	{
		LightProbe* probe = ::Helion::Render::OpenGL::Renderers::Legacy::World::Entities::FindLightProbe(level, (float)thing->X(), (float)thing->Y(), (float)thing->Center());
		if (probe)
		{
			draw.DynLight[0] = probe->Red;
			draw.DynLight[1] = probe->Green;
			draw.DynLight[2] = probe->Blue;
			draw.HasDynLight = true;
		}
	}

	if (draw.Translucent && m_sortedRenderer != nullptr)
	{
		float key;
		if (m_sortedRenderer->BspMode())
		{
			key = (float)m_sortedRenderer->Count();
		}
		else
		{
			const ::Helion::Geometry::Vec3F& view = renderInfo.Camera->PositionInterpolated;
			key = SortedTranslucentRenderer::DistanceSquared(draw.X, draw.Y, draw.Z, view.X, view.Y, view.Z);
		}

		m_sortedRenderer->Add(key, [this, draw](Shared::RenderInfo& info)
		{
			BufferMapScope maps;
			DrawModel(info, draw);
		});
	}
	else
	{
		m_opaque.push_back(draw);
	}

}

// ----------------------------------------------------------------------------------------------------------------
// Draw
// ----------------------------------------------------------------------------------------------------------------

void EntityModelRenderer::RenderOpaque(Shared::RenderInfo& renderInfo)
{
	if (m_opaque.empty())
		return;

	BufferMapScope maps;
	for (const ModelDraw& draw : m_opaque)
		DrawModel(renderInfo, draw);
}

void EntityModelRenderer::RenderHUDModel(Shared::RenderInfo& renderInfo, const HudModel& hud)
{
	if (hud.Frame == nullptr || hud.Weapon == nullptr)
		return;

	ModelDraw draw;
	draw.Hud = true;
	draw.HudData = hud;
	draw.Actor = hud.Camera;
	draw.Frame = hud.Frame;
	draw.TicFrac = hud.TicFrac;
	draw.RenderStyle = hud.RenderStyle;
	draw.Alpha = hud.Alpha;
	draw.LightLevel = hud.LightLevel;
	draw.LightBufferIndex = hud.LightBufferIndex;
	draw.Desaturation = hud.Desaturation;
	draw.ObjectColor = hud.ObjectColor;
	draw.AddColor = hud.AddColor;
	draw.PerPixelLights = hud.PerPixelLights;
	draw.DynLight[0] = hud.DynLight[0];
	draw.DynLight[1] = hud.DynLight[1];
	draw.DynLight[2] = hud.DynLight[2];
	draw.HasDynLight = hud.DynLight[0] != 0 || hud.DynLight[1] != 0 || hud.DynLight[2] != 0;
	draw.NoAlphaTest = true;
	draw.Translucent = !(hud.RenderStyle == DefaultRenderStyle());
	draw.Brightmap = !(hud.RenderStyle.Flags & STYLEF_ColorIsFixed);

	if (hud.Fullbright)
		draw.Options |= ModelOptions::Fullbright;
	if (hud.NoColoredLight)
		draw.Options |= ModelOptions::NoColoredLight;
	if (hud.RenderStyle.BlendOp == STYLEOP_Shadow || (hud.RenderStyle.BlendOp == STYLEOP_Add && hud.RenderStyle.DestAlpha == STYLEALPHA_One) ||
		(hud.RenderStyle.Flags & STYLEF_FadeToBlack))
		draw.Options |= ModelOptions::BlackFog;
	if (hud.RenderStyle.BlendOp == STYLEOP_Shadow)
	{
		draw.Alpha = 0.33f;
		draw.ObjectColor = PalEntry(255, 51, 51, 51);
		draw.AddColor = 0;
		draw.Options |= ModelOptions::Fullbright;
	}
	if (hud.RenderStyle.BlendOp == STYLEOP_RevSub || hud.RenderStyle.BlendOp == STYLEOP_Sub)
		draw.RenderStyle.BlendOp = STYLEOP_Fuzz;	// subtractive with models is not going to work.

	BufferMapScope maps;
	DrawModel(renderInfo, draw);
}

float EntityModelRenderer::AlphaThreshold(const ModelDraw& draw)
{
	// FRenderState::AlphaFunc(Alpha_GEqual, thresh): thresh - 0.001
	float thresh;
	if (draw.Hud)
		thresh = 0.f;	// DrawPSprite: AlphaFunc(Alpha_GEqual, 0) for models
	else if (!draw.Translucent)
		thresh = gl_mask_threshold;	// the model list is drawn with the mask threshold
	else if (draw.NoAlphaTest)
		thresh = 0.f;
	else
		thresh = gl_mask_sprite_threshold;
	return thresh - 0.001f;
}

void EntityModelRenderer::DrawModel(Shared::RenderInfo& renderInfo, const ModelDraw& draw)
{
	if (renderInfo.Viewpoint == nullptr)
		I_Error("EntityModelRenderer: the RenderInfo has no viewpoint uniforms");

	m_renderInfo = &renderInfo;
	m_state = screen->RenderState();
	m_draw = &draw;
	m_program = nullptr;
	m_material = nullptr;
	m_interpolation = 0;
	m_vertexBuffer = nullptr;
	m_indexBuffer = nullptr;
	m_boneStartIndex = -1;

	AActor* actor = draw.Actor;

	// hw_GetDynModelLight: the per actor light list of the models that are lit per pixel (the HUD model: of the camera).
	m_lightIndex = draw.PerPixelLights ? Lights::ModelLightList::Upload(actor) : -1;

	// The stream data of the draw (FRenderState::SetObjectColor / SetAddColor / SetDynLight / SetColorAlpha).
	m_stream = {};
	m_stream.uObjectColor = draw.ObjectColor;
	m_stream.uObjectColor2 = 0;
	m_stream.uAddColor = draw.AddColor;
	m_stream.uTextureAddColor = 0;
	m_stream.uTextureModulateColor = 0;
	m_stream.uTextureBlendColor = 0;
	m_stream.uFogColor = 0;
	m_stream.uDesaturationFactor = draw.Desaturation * (1.0f / 255.0f);
	m_stream.uVertexColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	m_stream.uDynLightColor = { draw.DynLight[0], draw.DynLight[1], draw.DynLight[2], 1.0f };

	// The render style as the blend of the program draws (FRenderState::SetRenderStyle), depth test / write are the caller's.
	SortedTranslucentRenderer::SetStyle(*m_state, draw.RenderStyle);

	// The world pass culls back faces (LegacyWorldRenderer CullBackFaces); models are drawn without culling unless
	// BeginDrawModel enables it (FHWModelRenderer runs inside the classic pass, which never culls).
	m_state->SetCulling(Cull_None);
	if (draw.Hud)
	{
		const HudModel& hud = draw.HudData;
		::RenderHUDModel(this, hud.Weapon, hud.Translation, hud.Rotation, hud.Pivot, hud.Frame, hud.TicFrac);
	}
	else
	{
		if (actor != nullptr && (actor->flags9 & MF9_DECOUPLEDANIMATIONS))
		{
			IFVIRTUALPTR(actor, AActor, AnimateBones)
			{
				CallVM<void>(func, actor, r_viewpoint.TicFrac);
			}
		}

		::RenderModel(this, draw.X, draw.Y, draw.Z, draw.Frame, actor, draw.TicFrac);
	}

	if (m_program != nullptr)
	{
		m_program->Unbind();
		m_program = nullptr;
	}
	m_state->SetInstanceBuffer(nullptr, 0);
	m_state->ClearCustomBlend();
	// back to the culling of the world pass (the HUD is drawn outside of it, without culling); a mirrored portal view culls the
	// other face (LegacyWorldRenderer s_windingFlipped)
	m_state->SetCulling(draw.Hud ? Cull_None : (InMirror ? Cull_CW : Cull_CCW));
	m_draw = nullptr;
	m_material = nullptr;
}

// ----------------------------------------------------------------------------------------------------------------
// FModelRenderer
// ----------------------------------------------------------------------------------------------------------------

VSMatrix EntityModelRenderer::GetViewToWorldMatrix()
{
	VSMatrix objectToWorldMatrix;
	VSMatrix viewMatrix = m_renderInfo->Viewpoint->ViewMatrix;
	viewMatrix.inverseMatrix(objectToWorldMatrix);
	return objectToWorldMatrix;
}

void EntityModelRenderer::BeginDrawModel(FRenderStyle style, int smf_flags, const VSMatrix& objectToWorldMatrix, bool mirrored)
{
	m_state->SetDepthFunc(DF_LEqual);
	// [BB] In case the model should be rendered translucent, do back face culling.
	// This solves a few of the problems caused by the lack of depth sorting.
	// [Nash] Don't do back face culling if explicitly specified in MODELDEF
	// TO-DO: Implement proper depth sorting.
	if ((smf_flags & MDL_FORCECULLBACKFACES) || (!(style == DefaultRenderStyle()) && !(smf_flags & MDL_DONTCULLBACKFACES)))
		m_state->SetCulling((mirrored ^ InMirror) ? Cull_CCW : Cull_CW);

	m_modelMatrix = objectToWorldMatrix;
	m_normalModelMatrix.computeNormalMatrix(objectToWorldMatrix);
	m_mirrored = mirrored;
}

void EntityModelRenderer::EndDrawModel(FRenderStyle style, int smf_flags)
{
	m_state->SetDepthFunc(DF_Less);
	if ((smf_flags & MDL_FORCECULLBACKFACES) || (!(style == DefaultRenderStyle()) && !(smf_flags & MDL_DONTCULLBACKFACES)))
		m_state->SetCulling(Cull_None);
}

void EntityModelRenderer::BeginDrawHUDModel(FRenderStyle style, const VSMatrix& objectToWorldMatrix, bool mirrored, int smf_flags)
{
	m_state->SetDepthFunc(DF_LEqual);
	m_oldDepthClamp = m_state->SetDepthClamp(true);

	// [BB] In case the model should be rendered translucent, do back face culling.
	// This solves a few of the problems caused by the lack of depth sorting.
	// TO-DO: Implement proper depth sorting.
	if (!(style == DefaultRenderStyle()) || (smf_flags & MDL_FORCECULLBACKFACES))
		m_state->SetCulling((mirrored ^ InMirror) ? Cull_CW : Cull_CCW);

	m_modelMatrix = objectToWorldMatrix;
	m_normalModelMatrix.computeNormalMatrix(objectToWorldMatrix);
	m_mirrored = mirrored;
}

void EntityModelRenderer::EndDrawHUDModel(FRenderStyle style, int smf_flags)
{
	m_state->SetDepthFunc(DF_Less);
	m_state->SetDepthClamp(m_oldDepthClamp);
	if (!(style == DefaultRenderStyle()) || (smf_flags & MDL_FORCECULLBACKFACES))
		m_state->SetCulling(Cull_None);
}

IModelVertexBuffer* EntityModelRenderer::CreateVertexBuffer(bool needindex, bool singleframe)
{
	return new FModelVertexBuffer(needindex, singleframe);
}

void EntityModelRenderer::SetInterpolation(double inter)
{
	m_interpolation = inter;
}

void EntityModelRenderer::SetMaterial(FGameTexture* skin, bool clampNoFilter, FTranslationID translation, AActor* act)
{
	m_material = nullptr;
	if (skin == nullptr)
		return;

	// FRenderState::SetMaterial(skin, UF_Skin, 0, clampmode, translation, -1, ...)
	int clampmode = clampNoFilter ? CLAMP_NOFILTER : CLAMP_NONE;
	int translationIndex = translation.index();
	int overrideShader = -1;
	EUpscaleFlags upscale = UF_Skin;
	int scaleflags = 0;

	skin->setSeen();
	if (!sysCallbacks.PreBindTexture || !sysCallbacks.PreBindTexture(m_state, skin, upscale, scaleflags, clampmode, translationIndex, overrideShader))
	{
		if (shouldUpscale(skin, upscale)) scaleflags |= CTF_Upscale;
	}

	FMaterial* mat = FMaterial::ValidateTexture(skin, scaleflags);
	if (mat == nullptr)
		return;

	// The material shader of the skin (FGLRenderState::ApplyMaterial: mEffectState).
	int shaderIndex = overrideShader >= 0 ? overrideShader : mat->GetShaderIndex();
	ModelProgram* program = m_programLookup ? m_programLookup(shaderIndex) : nullptr;
	if (program == nullptr && shaderIndex != 0 && m_programLookup)
		program = m_programLookup(0);
	if (program == nullptr)
		return;

	m_material = mat;
	m_translation = translationIndex;
	BindProgram(program);

	// The hardware textures of the layers (created by the backend when they are first used).
	screen->PrecacheMaterial(mat, translationIndex);

	FGameTexture* tex = mat->Source();
	int clamp = tex->GetClampMode(clampmode);
	m_clampMode = clamp;
	if (tex->isHardwareCanvas())
		static_cast<FCanvasTexture*>(tex->GetTexture())->NeedUpdate();

	MaterialLayerInfo* layer = nullptr;
	TextureUnits::BindTexture(BindTextures::BoundTexture, mat->GetLayer(0, translationIndex, &layer), clamp);
	m_program->Tex(BindTextures::BoundTexture);

	int numLayers = std::min(mat->NumLayers(), 12);
	for (int i = 1; i < numLayers; i++)
	{
		OpenGL::TextureUnit unit = (OpenGL::TextureUnit)(FirstLayerUnit + i - 1);
		TextureUnits::BindTexture(unit, mat->GetLayer(i, 0, &layer), clamp);
		m_program->MaterialTexture(i + 1, unit);
	}

	// FRenderState::GetTextureModeAndFlags
	int textureMode = SortedTranslucentRenderer::GetTextureMode(m_draw->RenderStyle);
	if (textureMode == TM_NORMAL && tex->isHardwareCanvas() && !tex->GetTranslucency())
		textureMode = TM_OPAQUE;
	int flags = mat->GetLayerFlags();
	if (!m_draw->Brightmap)
		flags &= ~(TEXF_Brightmap | TEXF_Glowmap);
	m_program->TextureMode(textureMode | flags);

	m_program->SpecularMaterial(tex->GetGlossiness(), tex->GetSpecularLevel());

	// SetMaterial: mStreamData.uDetailParms; the shader timer
	static uint64_t firstFrame = 0;
	if (firstFrame == 0)
		firstFrame = screen->FrameTime - 1;
	FVector2 detail = mat->GetDetailScale();
	m_stream.uDetailParms = { detail.X, detail.Y, 2, 0 };
	m_stream.timer = (float)((double)(screen->FrameTime - firstFrame) * (double)tex->GetShaderSpeed() / 1000.);
}

void EntityModelRenderer::BindProgram(ModelProgram* program)
{
	if (m_program == program)
		return;

	if (m_program != nullptr)
		m_program->Unbind();
	m_program = program;
	program->Bind();

	Shared::RenderInfo& info = *m_renderInfo;
	const auto& u = info.Uniforms;
	program->ColormapTexture(BindTextures::Colormap);
	program->SectorColormapTexture(BindTextures::SectorColormap);
	program->SectorFogTexture(BindTextures::SectorFog);
	program->SectorGlowTexture(BindTextures::SectorGlow);
	program->ExtraLight(u.ExtraLightOrColorMapIndex);
	program->HasInvulnerability(u.DrawInvulnerability);
	program->LightLevelMix(u.Mix);
	program->Mvp(u.Mvp);
	program->MvpNoPitch(u.MvpNoPitch);
	program->DistanceOffset(u.DistanceOffset);
	program->ColorMix(u.ColorMix.Global);
	program->LightMode(u.LightMode);
	program->GammaCorrection(u.GammaCorrection);
	program->UseSectorColor(u.SectorColor);
	program->UseSectorFog(u.SectorFogIndex);
	program->ViewpointUniforms(*info.Viewpoint);
	program->AlphaThreshold(AlphaThreshold(*m_draw));
	program->FogEnabled(0);
	program->FogBarrier(false);

	ApplyDrawUniforms();
}

void EntityModelRenderer::ApplyDrawUniforms()
{
	const ModelDraw& draw = *m_draw;
	m_program->ModelMatrix(m_modelMatrix);
	m_program->NormalModelMatrix(m_normalModelMatrix);
	m_program->InterpolationFactor((float)m_interpolation);
	m_program->ModelAlpha(draw.Alpha);
	m_program->ModelLightLevel((float)draw.LightLevel);
	m_program->LightBufferIndex(draw.LightBufferIndex);
	m_program->Options(draw.Options);
	m_program->DrawLights(m_lightIndex);
	// uLightAttr: x = uLightDist, y = uLightFactor, z = uFogDensity, w = uLightLevel (the fog is the sector fog of the Helion buffers).
	m_program->LightAttr(FVector4(1.0f / 31.0f, 1.0f, 0.0f, (draw.Options & ModelOptions::Fullbright) ? 1.0f : draw.LightLevel * (1.0f / 255.0f)));
	m_stream.uInterpolationFactor = (float)m_interpolation;
}

void EntityModelRenderer::SetupFrame(FModel* model, unsigned int frame1, unsigned int frame2, unsigned int size, int boneStartIndex)
{
	auto mdbuff = static_cast<FModelVertexBuffer*>(model->GetVertexBuffer(GetType()));
	m_boneStartIndex = boneStartIndex;
	m_frame1 = frame1;
	m_frame2 = frame2;
	m_vertexBuffer = mdbuff != nullptr ? mdbuff->vertexBuffer() : nullptr;
	m_indexBuffer = mdbuff != nullptr ? mdbuff->indexBuffer() : nullptr;
}

bool EntityModelRenderer::PrepareDraw()
{
	if (m_program == nullptr || m_material == nullptr || m_vertexBuffer == nullptr)
		return false;

	// The model matrix and the interpolation factor of the call (SetInterpolation comes per frame pair).
	ApplyDrawUniforms();
	m_program->BoneIndexBase(m_program->BindBones(m_boneStartIndex));
	m_program->MaterialStreamData(m_stream);

	// Binding point 0 of the program is the first frame, binding point 1 (the instance buffer slot, without a divisor) the second.
	m_state->SetVertexBuffer(m_vertexBuffer, m_frame1, m_frame2);
	m_state->SetInstanceBuffer(m_vertexBuffer, m_frame2);
	if (m_indexBuffer != nullptr)
		m_state->SetIndexBuffer(m_indexBuffer);

	m_program->FlushState();
	return true;
}

void EntityModelRenderer::DrawArrays(int start, int count)
{
	if (!PrepareDraw())
		return;

	m_state->Draw(DT_Triangles, start, count);
}

void EntityModelRenderer::DrawElements(int numIndices, size_t offset)
{
	if (!PrepareDraw())
		return;

	m_state->DrawIndexed(DT_Triangles, int(offset / sizeof(unsigned int)), numIndices);
}

}
