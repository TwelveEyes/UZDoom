/*
** WeaponRenderer.cpp
**
** UZDoom-only (plan M4.4): see WeaponRenderer.h. Port of hw_weapon.cpp.
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
#include <bit>
#include <cfloat>
#include <cmath>
#include "WeaponRenderer.h"
#include "BindTextures.h"
#include "Renderer.h"
#include "HelionCVars.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Static/LightBufferType.h"
#include "OpenGL/Renderers/Legacy/World/VertexOptions.h"
#include "OpenGL/Shader/TextureUnits.h"
#include "World/Geometry/Sectors/RenderSector.h"
#include "actor.h"
#include "actorinlines.h"
#include "d_net.h"
#include "d_player.h"
#include "doomstat.h"
#include "g_levellocals.h"
#include "gametexture.h"
#include "hw_cvars.h"
#include "hw_renderstate.h"
#include "hw_vrmodes.h"
#include "i_time.h"
#include "models.h"
#include "p_3dfloors.h"
#include "p_pspr.h"
#include "r_data/sprites.h"
#include "r_utility.h"
#include "sbar.h"
#include "texturemanager.h"
#include "v_video.h"
#include "vm.h"

EXTERN_CVAR(Float, transsouls)
EXTERN_CVAR(Int, gl_fuzztype)
EXTERN_CVAR(Bool, r_drawplayersprites)
EXTERN_CVAR(Bool, r_deathcamera)
EXTERN_CVAR(Bool, gl_light_sprites)
EXTERN_CVAR(Bool, gl_enhanced_nightvision)
EXTERN_CVAR(Float, gl_mask_sprite_threshold)
EXTERN_CVAR(Int, gl_weaponlight)
EXTERN_CVAR(Int, r_extralight)
EXTERN_CVAR(Int, screenblocks)

extern FGameTexture* CrosshairImage;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

using Data::SortedTranslucentRenderer;
using ::Helion::Render::OpenGL::Shader::TextureUnits;

namespace
{

constexpr int HwBrightPackAlpha = 255;

struct WeaponPosition2D
{
	float wx, wy;
	float bobx, boby;
	DPSprite* weapon;
};

struct WeaponPosition3D
{
	float wx, wy;
	FVector3 translation, rotation, pivot;
	DPSprite* weapon;
};

// hw_weapon.cpp: the bright flag of the state (and the texture's fullbright disable).
bool IsBright(DPSprite* psp)
{
	if (psp != nullptr && psp->GetState() != nullptr)
	{
		bool disablefullbright = false;
		FTextureID lump = sprites[psp->GetSprite()].GetSpriteFrame(psp->GetFrame(), 0, nullAngle, nullptr);
		if (lump.isValid())
		{
			auto tex = TexMan.GetGameTexture(lump, true);
			if (tex) disablefullbright = tex->isFullbrightDisabled();
		}
		return psp->GetState()->GetFullbright() && !disablefullbright;
	}
	return false;
}

float PackColor(PalEntry color)
{
	return std::bit_cast<float>((uint32_t)color.r | ((uint32_t)color.g << 8) | ((uint32_t)color.b << 16) | ((uint32_t)color.a << 24));
}

WeaponPosition2D GetWeaponPosition2D(player_t* player, double ticFrac)
{
	WeaponPosition2D w;
	BobType = PSPB_2D;
	FVector2 interp = PlayerBob[player - players].Interpolate2D(Net_ModifyFrac(ticFrac));
	w.bobx = interp.X;
	w.boby = interp.Y;

	// Interpolate the main weapon layer once so as to be able to add it to other layers.
	if ((w.weapon = player->FindPSprite(PSP_WEAPON)) != nullptr)
	{
		if (w.weapon->firstTic)
		{
			w.wx = (float)w.weapon->x;
			w.wy = (float)w.weapon->y;
		}
		else
		{
			const double frac = Net_ModifyObjectFrac(w.weapon, ticFrac);
			w.wx = (float)(w.weapon->oldx + (w.weapon->x - w.weapon->oldx) * frac);
			w.wy = (float)(w.weapon->oldy + (w.weapon->y - w.weapon->oldy) * frac);
		}
	}
	else
	{
		w.wx = 0;
		w.wy = 0;
	}
	return w;
}

WeaponPosition3D GetWeaponPosition3D(player_t* player, double ticFrac)
{
	WeaponPosition3D w;
	BobType = PSPB_3D;
	PlayerBob[player - players].Interpolate3D(w.translation, w.rotation, Net_ModifyFrac(ticFrac));

	if ((w.weapon = player->FindPSprite(PSP_WEAPON)) != nullptr)
	{
		if (w.weapon->firstTic)
		{
			w.wx = (float)w.weapon->x;
			w.wy = (float)w.weapon->y;
		}
		else
		{
			const double frac = Net_ModifyObjectFrac(w.weapon, ticFrac);
			w.wx = (float)(w.weapon->oldx + (w.weapon->x - w.weapon->oldx) * frac);
			w.wy = (float)(w.weapon->oldy + (w.weapon->y - w.weapon->oldy) * frac);
		}

		auto weaponActor = w.weapon->GetCaller();
		if (weaponActor && weaponActor->IsKindOf(NAME_Weapon))
		{
			DVector3* dPivot = (DVector3*)weaponActor->ScriptVar(NAME_BobPivot3D, nullptr);
			w.pivot.X = (float)dPivot->X;
			w.pivot.Y = (float)dPivot->Y;
			w.pivot.Z = (float)dPivot->Z;
		}
		else
		{
			w.pivot = FVector3(0, 0, 0);
		}
	}
	else
	{
		w.wx = 0;
		w.wy = 0;
		w.pivot = FVector3(0, 0, 0);
	}
	return w;
}

FVector2 BobWeapon2D(WeaponPosition2D& weap, DPSprite* psp, double ticFrac)
{
	if (psp->firstTic)
	{ // Can't interpolate the first tic.
		psp->firstTic = false;
		psp->ResetInterpolation();
	}

	float sx = float(psp->oldx + (psp->x - psp->oldx) * ticFrac);
	float sy = float(psp->oldy + (psp->y - psp->oldy) * ticFrac);

	if (psp->Flags & PSPF_ADDBOB)
	{
		sx += (psp->Flags & PSPF_MIRROR) ? -weap.bobx : weap.bobx;
		sy += weap.boby;
	}

	if (psp->Flags & PSPF_ADDWEAPON && psp->GetID() != PSP_WEAPON)
	{
		sx += weap.wx;
		sy += weap.wy;
	}
	return { sx, sy };
}

FVector2 BobWeapon3D(WeaponPosition3D& weap, DPSprite* psp, FVector3& translation, FVector3& rotation, FVector3& pivot, double ticFrac)
{
	if (psp->firstTic)
	{ // Can't interpolate the first tic.
		psp->firstTic = false;
		psp->ResetInterpolation();
	}

	float sx = float(psp->oldx + (psp->x - psp->oldx) * ticFrac);
	float sy = float(psp->oldy + (psp->y - psp->oldy) * ticFrac);

	if (psp->Flags & PSPF_ADDBOB)
	{
		if (psp->Flags & PSPF_MIRROR)
		{
			translation = FVector3(-weap.translation.X, weap.translation.Y, weap.translation.Z);
			rotation = FVector3(-weap.rotation.X, weap.rotation.Y, weap.rotation.Z);
			pivot = FVector3(-weap.pivot.X, weap.pivot.Y, weap.pivot.Z);
		}
		else
		{
			translation = weap.translation;
			rotation = weap.rotation;
			pivot = weap.pivot;
		}
	}
	else
	{
		translation = rotation = pivot = FVector3(0, 0, 0);
	}

	if (psp->Flags & PSPF_ADDWEAPON && psp->GetID() != PSP_WEAPON)
	{
		sx += weap.wx;
		sy += weap.wy;
	}
	return { sx, sy };
}

}

WeaponRenderer::WeaponRenderer(Texture::Legacy::LegacyGLTextureManager& textureManager, EntityProgram* program, EntityModelRenderer* modelRenderer)
	: m_textureManager(textureManager), m_program(program), m_modelRenderer(modelRenderer)
{
	m_pool = std::make_unique<Data::RenderDataPool<EntityVertex>>(*m_program, 16, 1);
	m_paletteColorMode = textureManager.PaletteColorMode();
}

WeaponRenderer::~WeaponRenderer() = default;

void WeaponRenderer::Clear()
{
	for (auto* data : m_used)
	{
		data->Clear();
		m_pool->Return(data);
	}
	m_used.clear();
	m_items.clear();
	m_sortCounter = 0;
	m_depthCleared = false;
}

// ----------------------------------------------------------------------------------------------------------------
// Lighting. The light level stays the sector light level: the shader applies ExtraLight, the light mode and the sector colour / fog.
// ----------------------------------------------------------------------------------------------------------------

WeaponRenderer::WeaponLight WeaponRenderer::GetWeaponLighting(Shared::RenderInfo& renderInfo, sector_t* viewsector, const DVector3& pos, bool fullbrightScene, const DVector3& playerpos)
{
	using Geometry::Static::LightBufferType;
	WeaponLight l;
	FLevelLocals* level = viewsector->Level;
	l.LightBufferIndex = Renderer::GetLightBufferIndex(viewsector, viewsector->e->XFloor.ffloors.Size() > 0 ? LightBufferType::Wall : LightBufferType::Floor);

	if (fullbrightScene)
	{
		l.LightLevel = 255;
		l.IsBelow = false;
		return l;
	}

	auto renderSector = ::Helion::World::Geometry::Sectors::GetRenderSector(viewsector, renderInfo.TransferHeightView);
	sector_t* fakesec = renderSector.Sector != nullptr ? renderSector.Sector : viewsector;

	// UZDoom truecolor: the light level of classic GetWeaponLighting (RescaleLightLevel, CalcLightLevel with the weapon flag
	// and the Korshun remap below). The entity program uses it as the colour of HUD sprites (SetColor(weapon), no fog).
	const bool truecolor = !m_paletteColorMode;
	// RescaleLightLevel (hw_lighting.h): r_extralight
	auto rescale = [](int lightlevel) { int extralight = r_extralight; return std::max(std::clamp(lightlevel, 0, 255) * (255 - extralight) / 255 + extralight, 0); };
	l.LightLevel = truecolor ? rescale(fakesec->lightlevel) : fakesec->lightlevel;
	FColormap cm;
	cm.Clear();
	l.Desaturation = 0;

	// calculate colormap for weapon sprites
	if (viewsector->e->XFloor.ffloors.Size() && !(level->flags3 & LEVEL3_NOCOLOREDSPRITELIGHTING))
	{
		TArray<lightlist_t>& lightlist = viewsector->e->XFloor.lightlist;
		for (unsigned i = 0; i < lightlist.Size(); i++)
		{
			double lightbottom;
			if (i < lightlist.Size() - 1)
				lightbottom = lightlist[i + 1].plane.ZatPoint(pos);
			else
				lightbottom = viewsector->floorplane.ZatPoint(pos);

			if (lightbottom < pos.Z)
			{
				cm = lightlist[i].extra_colormap;
				l.LightLevel = truecolor ? rescale(*lightlist[i].p_lightlevel) : *lightlist[i].p_lightlevel;
				if (lightlist[i].caster != nullptr)
					l.LightBufferIndex = Renderer::GetLightBufferIndex(lightlist[i].caster->model, LightBufferType::Wall);
				break;
			}
		}
	}
	else
	{
		cm = fakesec->Colormap;
		if (level->flags3 & LEVEL3_NOCOLOREDSPRITELIGHTING)
		{
			cm.ClearColor();
			l.NoColoredLight = true;
		}
	}
	l.Desaturation = cm.Desaturation;

	if (truecolor)
	{
		// CalcLightLevel(lightmode, lightlevel, getExtraLight(), weapon = true): no dark light mode adjustment for weapons
		int light = l.LightLevel <= 0 ? 0 : std::clamp(l.LightLevel + (int)(r_viewpoint.extralight * gl_weaponlight), 1, 255);
		ELightMode lightmode = getRealLightmode(level, true);
		const bool software = lightmode == ELightMode::ZDoomSoftware || lightmode == ELightMode::DoomSoftware || lightmode == ELightMode::Build;
		if (software || light < 92)
		{
			// Korshun: the way based on max possible light level for sector like in software renderer.
			double min_L = 36.0 / 31.0 - ((light / 255.0) * (63.0 / 31.0)); // Lightlevel in range 0-63
			min_L = std::clamp(min_L, 0.0, 1.0);
			light = int((1.0 - min_L) * 255);
		}
		else
		{
			light = (2 * light + 255) / 3;
		}
		l.LightLevel = light;
	}
	l.LightLevel = std::clamp(viewsector->CheckSpriteGlow(l.LightLevel, playerpos), 0, 255);
	l.IsBelow = fakesec != viewsector && renderInfo.TransferHeightView == ::Helion::World::Geometry::Sectors::TransferHeightView::Bottom;

	// Korshun: fullbright fog in opengl, render weapon sprites fullbright (but don't cancel out the light color!)
	if (level->brightfog && ((level->flags & LEVEL_HASFADETABLE) || cm.FadeColor != 0))
		l.LightLevel = 255;
	return l;
}

// ----------------------------------------------------------------------------------------------------------------
// Render style
// ----------------------------------------------------------------------------------------------------------------

bool WeaponRenderer::GetWeaponRenderStyle(HudItem& item, DPSprite* psp, AActor* playermo, sector_t* viewsector, const WeaponLight& light, double ticFrac)
{
	auto rs = psp->GetRenderStyle(playermo->RenderStyle, playermo->InterpolatedAlpha(ticFrac));

	visstyle_t vis;
	vis.RenderStyle = STYLE_Count;
	vis.Alpha = rs.second;
	vis.Invert = false;
	playermo->AlterWeaponSprite(&vis);

	item.Alpha = (psp->Flags & PSPF_FORCEALPHA) ? 0.f : vis.Alpha;

	if (vis.RenderStyle != STYLE_Count && !(psp->Flags & PSPF_FORCESTYLE))
		item.RenderStyle = vis.RenderStyle;
	else
		item.RenderStyle = rs.first;
	if (item.RenderStyle.BlendOp == STYLEOP_None) return false;

	if (vis.Invert)
	{
		// this only happens for Strife's inverted weapon sprite
		item.RenderStyle.Flags |= STYLEF_InvertSource;
	}

	item.OverrideFuzz = 0;
	if (item.RenderStyle.BlendOp == STYLEOP_Fuzz)
	{
		if (gl_fuzztype != 0)
		{
			item.RenderStyle = LegacyRenderStyles[STYLE_Translucent];
			item.OverrideFuzz = gl_fuzztype;
			item.Alpha = 0.99f;	// trans may not be 1 here
		}
		else
		{
			item.RenderStyle.BlendOp = STYLEOP_Shadow;
		}
	}

	if (item.RenderStyle.Flags & STYLEF_TransSoulsAlpha)
		item.Alpha = transsouls;
	else if (item.RenderStyle.Flags & STYLEF_Alpha1)
		item.Alpha = 1.f;
	else if (item.Alpha == 0.f)
		item.Alpha = vis.Alpha;
	if (!item.RenderStyle.IsVisible(item.Alpha)) return false;	// if it isn't visible skip the rest.

	PalEntry thingColor = (playermo->RenderStyle.Flags & STYLEF_ColorIsFixed) ? PalEntry(playermo->fillcolor) : PalEntry(255, 255, 255);
	thingColor.a = 255;

	const bool bright = IsBright(psp);
	item.ObjectColor = bright ? thingColor : thingColor.Modulate(viewsector->SpecialColors[sector_t::sprites]);

	item.LightLevel = light.LightLevel;
	item.Desaturation = light.Desaturation;
	item.NoColoredLight = light.NoColoredLight;
	item.LightBufferIndex = light.LightBufferIndex;
	item.Fullbright = false;
	if (bright)
	{
		// SetBright: 255, white light colour (under water areas keep most of their colour: not representable, the sector colour is applied by the buffer).
		item.LightLevel = 255;
		item.Fullbright = !light.IsBelow;
	}

	if (playermo->Sector)
		item.AddColor = playermo->Sector->AdditiveColors[sector_t::sprites] | 0xff000000;
	else
		item.AddColor = 0;
	return true;
}

// ----------------------------------------------------------------------------------------------------------------
// Coordinates
// ----------------------------------------------------------------------------------------------------------------

bool WeaponRenderer::GetWeaponRect(HudItem& item, DPSprite* psp, float sx, float sy, double ticfrac)
{
	// decide which patch to use
	bool mirror;
	FTextureID lump = sprites[psp->GetSprite()].GetSpriteFrame(psp->GetFrame(), 0, nullAngle, &mirror);
	if (!lump.isValid()) return false;

	auto tex = TexMan.GetGameTexture(lump, false);
	if (!tex || !tex->isValid()) return false;

	FTranslationID trans = psp->GetTranslation();
	if (psp->Flags & PSPF_PLAYERTRANSLATED)
		trans = players[consoleplayer].camera->Translation;
	const int translation = trans.index();
	const auto* spriteRotation = m_textureManager.GetSpriteRotation(lump, mirror, translation);
	if (spriteRotation == nullptr || spriteRotation->RenderStore == nullptr)
		return false;

	GLLegacyTexture* texture = spriteRotation->RenderStore;
	const int imageW = texture->ImageDimension.Width > 0 ? texture->ImageDimension.Width : std::max(texture->Width(), 1);
	const int imageH = texture->ImageDimension.Height > 0 ? texture->ImageDimension.Height : std::max(texture->Height(), 1);
	const float foldX = 1.0f / tex->GetScaleX();
	const float foldY = 1.0f / tex->GetScaleY();
	// FGameTexture::SetSpriteRect uses the r_spriteadjust adjusted offsets (sprofs).
	const ::Helion::Geometry::Vec2I offset(tex->GetTexelLeftOffset(r_spriteadjustHW), tex->GetTexelTopOffset(r_spriteadjustHW));

	// FloatRect of FGameTexture::SetSpriteRect, from the texel size and offsets of the layer
	struct { float left, top, width, height; } r;
	r.left = -offset.X * foldX;
	r.top = -offset.Y * foldY;
	r.width = imageW * foldX;
	r.height = imageH * foldY;

	float vw = (float)viewwidth;
	float vh = (float)viewheight;

	// calculate edges of the shape
	float scalex = psp->baseScale.X * (320.0f / (240.0f * r_viewwindow.WidescreenRatio)) * (vw / 320);

	float tx = (psp->Flags & PSPF_MIRROR) ? ((160 - r.width) - (sx + r.left)) : (sx - (160 - r.left));
	float x1 = tx * scalex + vw / 2;
	x1 += viewwindowx;

	tx += r.width;
	float x2 = tx * scalex + vw / 2;
	x2 += viewwindowx;

	// killough 12/98: fix psprite positioning problem
	float ftextureadj = (120.0f / psp->baseScale.Y) - 100.0f; // [XA] scale relative to weapon baseline
	float ftexturemid = 100.f - sy - r.top - psp->GetYAdjust(screenblocks >= 11) - ftextureadj;

	float scale = psp->baseScale.Y * (SCREENHEIGHT * vw) / (SCREENWIDTH * 240.0f);

	// Canvas textures are stored upside down
	if (tex->isHardwareCanvas()) scale *= -1;

	float y1 = viewwindowy + vh / 2 - (ftexturemid * scale);
	float y2 = y1 + (r.height * scale) + 1;

	const bool flip = (psp->Flags & PSPF_FLIP);

	// [MC] Code copied from DTA_Rotate.
	WeaponInterp Vert;
	Vert.v[0] = FVector2(x1, y1);
	Vert.v[1] = FVector2(x1, y2);
	Vert.v[2] = FVector2(x2, y1);
	Vert.v[3] = FVector2(x2, y2);

	for (int i = 0; i < 4; i++)
	{
		const float cx = (flip) ? -psp->Coord[i].X : psp->Coord[i].X;
		Vert.v[i] += FVector2(cx * scalex, psp->Coord[i].Y * scale);
	}
	if (psp->rotation != nullAngle || !psp->scale.isZero())
	{
		// [MC] Sets up the alignment for starting the pivot at, in a corner.
		float anchorx, anchory;
		switch (psp->VAlign)
		{
			default:
			case PSPA_TOP:		anchory = 0.0;	break;
			case PSPA_CENTER:	anchory = 0.5;	break;
			case PSPA_BOTTOM:	anchory = 1.0;	break;
		}

		switch (psp->HAlign)
		{
			default:
			case PSPA_LEFT:		anchorx = 0.0;	break;
			case PSPA_CENTER:	anchorx = 0.5;	break;
			case PSPA_RIGHT:	anchorx = 1.0;	break;
		}
		// Handle PSPF_FLIP.
		if (flip) anchorx = 1.0 - anchorx;

		FAngle rot = FAngle::fromDeg(float((flip) ? -psp->rotation.Degrees() : psp->rotation.Degrees()));
		const float cosang = rot.Cos();
		const float sinang = rot.Sin();

		float xcenter, ycenter;
		const float width = x2 - x1;
		const float height = y2 - y1;
		const float px = float((flip) ? -psp->pivot.X : psp->pivot.X);
		const float py = float(psp->pivot.Y);

		// Set up the center and offset accordingly. PivotPercent changes it to be a range [0.0, 1.0]
		// instead of pixels and is enabled by default.
		if (psp->Flags & PSPF_PIVOTPERCENT)
		{
			xcenter = x1 + (width * anchorx + width * px);
			ycenter = y1 + (height * anchory + height * py);
		}
		else
		{
			xcenter = x1 + (width * anchorx + scalex * px);
			ycenter = y1 + (height * anchory + scale * py);
		}

		// Now adjust the position, rotation and scale of the image based on the latter two.
		for (int i = 0; i < 4; i++)
		{
			Vert.v[i] -= {xcenter, ycenter};
			const float xx = xcenter + psp->scale.X * (Vert.v[i].X * cosang + Vert.v[i].Y * sinang);
			const float yy = ycenter - psp->scale.Y * (Vert.v[i].X * sinang - Vert.v[i].Y * cosang);
			Vert.v[i] = {xx, yy};
		}
	}
	psp->Vert = Vert;

	if (psp->scale.X == 0.0 || psp->scale.Y == 0.0)
		return false;

	const bool interp = (psp->InterpolateTic || psp->Flags & PSPF_INTERPOLATE);

	for (int i = 0; i < 4; i++)
	{
		FVector2 t = Vert.v[i];
		if (interp)
			t = psp->Prev.v[i] + (psp->Vert.v[i] - psp->Prev.v[i]) * ticfrac;

		item.Corner[i][0] = t.X;
		item.Corner[i][1] = t.Y;
	}

	// u1 / u2 swap of the original (mirror xor flip)
	item.FlipU = !(mirror) != !(flip);
	item.GameTexture = tex;
	item.Texture = texture;
	item.Brightmap = spriteRotation->BrightmapRenderStore;
	item.Translation = translation;
	return true;
}

// ----------------------------------------------------------------------------------------------------------------
// Prepare
// ----------------------------------------------------------------------------------------------------------------

void WeaponRenderer::PreparePlayerSprites2D(Shared::RenderInfo& renderInfo, sector_t* viewsector)
{
	static PClass* wpCls = PClass::FindClass("Weapon");
	static unsigned ModifyBobLayerVIndex = GetVirtualIndex(wpCls, "ModifyBobLayer");
	static VMFunction* ModifyBobLayerOrigFunc = wpCls->Virtuals.Size() > ModifyBobLayerVIndex ? wpCls->Virtuals[ModifyBobLayerVIndex] : nullptr;

	AActor* playermo = players[consoleplayer].camera;
	player_t* player = playermo->player;
	const auto& vp = r_viewpoint;
	AActor* camera = vp.camera;
	FLevelLocals* level = camera->Level;

	WeaponPosition2D weap = GetWeaponPosition2D(camera->player, vp.TicFrac);
	WeaponLight light = GetWeaponLighting(renderInfo, viewsector, vp.Pos, m_fullbright.Fullbright, camera->Pos());

	VMFunction* ModifyBobLayer = nullptr;
	DVector2 bobxy = DVector2(weap.bobx, weap.boby);

	if (weap.weapon && weap.weapon->GetCaller())
	{
		PClass* cls = weap.weapon->GetCaller()->GetClass();
		ModifyBobLayer = cls->Virtuals.Size() > ModifyBobLayerVIndex ? cls->Virtuals[ModifyBobLayerVIndex] : nullptr;
		if (ModifyBobLayer == ModifyBobLayerOrigFunc) ModifyBobLayer = nullptr;
	}

	const double bobFrac = Net_ModifyFrac(vp.TicFrac);
	for (DPSprite* psp = player->psprites; psp != nullptr && psp->GetID() < PSP_TARGETCENTER; psp = psp->GetNext())
	{
		if (!psp->GetState()) continue;

		FSpriteModelFrame* smf = FindModelFrame(psp->Caller, psp->GetSprite(), psp->GetFrame(), false);

		// This is an 'either-or' proposition. This maybe needs some work to allow overlays with weapon models but as originally implemented this just won't work.
		if (smf) continue;

		HudItem item;
		item.Weapon = psp;

		if (!GetWeaponRenderStyle(item, psp, camera, viewsector, light, vp.TicFrac)) continue;

		if (ModifyBobLayer && (psp->Flags & PSPF_ADDBOB))
		{
			DVector2 out;
			VMValue param[] = { weap.weapon->GetCaller(), bobxy.X, bobxy.Y, psp->GetID(), bobFrac };
			VMReturn ret(&out);

			VMCall(ModifyBobLayer, param, 5, &ret, 1);

			weap.bobx = out.X;
			weap.boby = out.Y;
		}

		const double frac = Net_ModifyObjectFrac(psp, vp.TicFrac);
		FVector2 spos = BobWeapon2D(weap, psp, frac);

		// set the lighting parameters
		if (item.RenderStyle.BlendOp != STYLEOP_Shadow && level->HasDynamicLights && !m_fullbright.Fullbright && gl_light_sprites)
		{
			GetDynSpriteLight(level, playermo, nullptr, item.DynRgb);
			item.HasDyn = true;
		}

		if (!GetWeaponRect(item, psp, spos.X, spos.Y, std::min<double>(bobFrac, frac))) continue;
		m_items.push_back(item);
	}
}

void WeaponRenderer::PreparePlayerSprites3D(Shared::RenderInfo& renderInfo, sector_t* viewsector)
{
	static PClass* wpCls = PClass::FindClass("Weapon");

	static unsigned ModifyBobLayer3DVIndex = GetVirtualIndex(wpCls, "ModifyBobLayer3D");
	static unsigned ModifyBobPivotLayer3DVIndex = GetVirtualIndex(wpCls, "ModifyBobPivotLayer3D");

	static VMFunction* ModifyBobLayer3DOrigFunc = wpCls->Virtuals.Size() > ModifyBobLayer3DVIndex ? wpCls->Virtuals[ModifyBobLayer3DVIndex] : nullptr;
	static VMFunction* ModifyBobPivotLayer3DOrigFunc = wpCls->Virtuals.Size() > ModifyBobPivotLayer3DVIndex ? wpCls->Virtuals[ModifyBobPivotLayer3DVIndex] : nullptr;

	AActor* playermo = players[consoleplayer].camera;
	player_t* player = playermo->player;
	const auto& vp = r_viewpoint;
	AActor* camera = vp.camera;
	FLevelLocals* level = camera->Level;

	WeaponPosition3D weap = GetWeaponPosition3D(camera->player, vp.TicFrac);
	WeaponLight light = GetWeaponLighting(renderInfo, viewsector, vp.Pos, m_fullbright.Fullbright, camera->Pos());

	VMFunction* ModifyBobLayer3D = nullptr;
	VMFunction* ModifyBobPivotLayer3D = nullptr;

	DVector3 translation = DVector3(weap.translation);
	DVector3 rotation = DVector3(weap.rotation);
	DVector3 pivot = DVector3(weap.pivot);

	if (weap.weapon && weap.weapon->GetCaller())
	{
		PClass* cls = weap.weapon->GetCaller()->GetClass();
		ModifyBobLayer3D = cls->Virtuals.Size() > ModifyBobLayer3DVIndex ? cls->Virtuals[ModifyBobLayer3DVIndex] : nullptr;
		ModifyBobPivotLayer3D = cls->Virtuals.Size() > ModifyBobPivotLayer3DVIndex ? cls->Virtuals[ModifyBobPivotLayer3DVIndex] : nullptr;

		if (ModifyBobLayer3D == ModifyBobLayer3DOrigFunc) ModifyBobLayer3D = nullptr;
		if (ModifyBobPivotLayer3D == ModifyBobPivotLayer3DOrigFunc) ModifyBobPivotLayer3D = nullptr;
	}

	const double bobFrac = Net_ModifyFrac(vp.TicFrac);
	for (DPSprite* psp = player->psprites; psp != nullptr && psp->GetID() < PSP_TARGETCENTER; psp = psp->GetNext())
	{
		if (!psp->GetState()) continue;
		FSpriteModelFrame* smf = FindModelFrame(psp->Caller, psp->GetSprite(), psp->GetFrame(), false);

		// This is an 'either-or' proposition. This maybe needs some work to allow overlays with weapon models but as originally implemented this just won't work.
		if (!smf) continue;

		HudItem item;
		item.Weapon = psp;
		item.ModelFrame = smf;

		int smf_flags = smf->getFlags(psp->Caller->modelData);

		if (ModifyBobLayer3D && (psp->Flags & PSPF_ADDBOB))
		{
			DVector3 t, r;

			VMReturn returns[2];

			returns[0].Vec3At(&t);
			returns[1].Vec3At(&r);

			VMValue param[] = { weap.weapon->GetCaller(), translation.X, translation.Y, translation.Z, rotation.X, rotation.Y, rotation.Z, psp->GetID(), bobFrac };
			VMCall(ModifyBobLayer3D, param, 9, returns, 2);

			weap.translation = FVector3(t);
			weap.rotation = FVector3(r);
		}

		if (ModifyBobPivotLayer3D && (psp->Flags & PSPF_ADDBOB))
		{
			DVector3 p;

			VMReturn ret(&p);

			VMValue param[] = { weap.weapon->GetCaller(), pivot.X, pivot.Y, pivot.Z, psp->GetID(), bobFrac };
			VMCall(ModifyBobPivotLayer3D, param, 6, &ret, 1);

			weap.pivot = FVector3(p);
		}

		if (!GetWeaponRenderStyle(item, psp, camera, viewsector, light, vp.TicFrac)) continue;

		FVector2 spos = BobWeapon3D(weap, psp, item.ModelTranslation, item.ModelRotation, item.ModelPivot, Net_ModifyObjectFrac(psp, vp.TicFrac));

		// set the lighting parameters
		if (item.RenderStyle.BlendOp != STYLEOP_Shadow && level->HasDynamicLights && !m_fullbright.Fullbright && gl_light_sprites && !(playermo->renderflags2 & RF2_NODYNAMICLIGHTING))
		{
			if (!(smf_flags & MDL_NOPERPIXELLIGHTING))
			{
				item.PerPixelLights = true;
			}
			else
			{
				GetDynSpriteLight(level, playermo, nullptr, item.DynRgb);
				item.HasDyn = true;
			}
			LightProbe* probe = Entities::FindLightProbe(level, playermo->X(), playermo->Y(), playermo->Center());
			if (probe)
			{
				item.DynRgb[0] = probe->Red;
				item.DynRgb[1] = probe->Green;
				item.DynRgb[2] = probe->Blue;
				item.HasDyn = true;
			}
		}

		// [BB] In the HUD model step we just render the model and break out.
		item.Mx = spos.X;
		item.My = spos.Y;

		m_items.push_back(item);
	}
}

void WeaponRenderer::PrepareTargeterSprites(Shared::RenderInfo& renderInfo, sector_t* viewsector, double ticfrac)
{
	AActor* playermo = players[consoleplayer].camera;
	player_t* player = playermo->player;

	WeaponLight light;
	light.LightLevel = 255;
	light.LightBufferIndex = Renderer::GetLightBufferIndex(viewsector, Geometry::Static::LightBufferType::Floor);

	// The Targeter's sprites are always drawn normally.
	for (DPSprite* psp = player->FindPSprite(PSP_TARGETCENTER); psp != nullptr; psp = psp->GetNext())
	{
		if (psp->GetState() != nullptr && (psp->GetID() != PSP_TARGETCENTER || CrosshairImage == nullptr))
		{
			HudItem item;
			item.Weapon = psp;
			item.LightLevel = 255;
			item.Fullbright = true;
			item.LightBufferIndex = light.LightBufferIndex;
			item.ObjectColor = 0xffffffff;
			item.Alpha = 1;
			item.RenderStyle = DefaultRenderStyle();

			if (GetWeaponRect(item, psp, psp->x, psp->y, Net_ModifyObjectFrac(psp, ticfrac)))
				m_items.push_back(item);
		}
	}
}

// ----------------------------------------------------------------------------------------------------------------
// Draw
// ----------------------------------------------------------------------------------------------------------------

bool WeaponRenderer::IsSolid(const HudItem& item) const
{
	const FRenderStyle& rs = item.RenderStyle;
	if (item.OverrideFuzz != 0 || rs.BlendOp != STYLEOP_Add)
		return false;
	if (rs.Flags & (STYLEF_RedIsAlpha | STYLEF_ColorIsFixed))
		return false;
	if (item.GameTexture != nullptr && item.GameTexture->GetTranslucency())
		return false;

	if (rs.SrcAlpha == STYLEALPHA_One && rs.DestAlpha == STYLEALPHA_Zero)
		return true;
	return rs.SrcAlpha == STYLEALPHA_Src && rs.DestAlpha == STYLEALPHA_InvSrc && item.Alpha >= 1.0f - FLT_EPSILON;
}

EntityVertex WeaponRenderer::MakeVertex(const HudItem& item, float*)
{
	using Opts = EntityOptions;
	const FRenderStyle& rs = item.RenderStyle;
	const bool shadow = rs.BlendOp == STYLEOP_Shadow;

	int options = 0;
	if (item.OverrideFuzz != 0 || (item.GameTexture != nullptr && item.GameTexture->GetTranslucency()) || (rs.Flags & STYLEF_RedIsAlpha))
		options |= Opts::NoAlphaTest;
	if (rs.Flags & STYLEF_RedIsAlpha)
		options |= Opts::AlphaTexture;
	else if (rs.Flags & STYLEF_ColorIsFixed)
		options |= Opts::Stencil;
	if (rs.Flags & STYLEF_InvertSource)
		options |= Opts::InvertSource;
	if (rs.Flags & STYLEF_FadeToBlack)
		options |= Opts::FadeToBlack;
	if (rs.BlendOp == STYLEOP_Sub || rs.BlendOp == STYLEOP_RevSub)
		options |= Opts::SubtractiveStyle;
	if (item.Fullbright || shadow)
		options |= Opts::FullbrightLight;
	if (item.NoColoredLight)
		options |= Opts::NoColoredLight;

	float alpha = item.Alpha;
	PalEntry objectColor = item.ObjectColor;
	if (shadow)
	{
		// SetColor(0.2, 0.2, 0.2, 0.33): a gray, unlit translucent quad
		alpha = 0.33f;
		objectColor = PalEntry(255, 51, 51, 51);
	}

	const int colorMapIndex = m_paletteColorMode ? (item.Translation & 0x3FFF) : 0;
	const int imageW = item.Texture->ImageDimension.Width > 0 ? item.Texture->ImageDimension.Width : std::max(item.Texture->Width(), 1);
	const int imageH = item.Texture->ImageDimension.Height > 0 ? item.Texture->ImageDimension.Height : std::max(item.Texture->Height(), 1);

	EntityVertex vertex = {};
	vertex.PosX = item.Corner[0][0];
	vertex.PosY = item.Corner[0][1];
	vertex.PosZ = 0;
	vertex.PrevPosX = item.Corner[1][0];
	vertex.PrevPosY = item.Corner[1][1];
	vertex.PrevPosZ = 0;
	vertex.AngleYaw = item.Corner[2][0];
	vertex.AnglePitch = item.Corner[2][1];
	vertex.SpriteOffsetX = item.Corner[3][0];
	vertex.SpriteOffsetY = item.Corner[3][1];
	vertex.SurfaceOptions = VertexOptions::EntityPackSurface(alpha, item.OverrideFuzz != 0 ? 1 : 0, item.FlipU ? 1 : 0, colorMapIndex, item.LightLevel);
	vertex.RenderOptions = VertexOptions::EntityPackRender(item.LightBufferIndex, 0);
	vertex.TextureInfo = VertexOptions::EntityPackTextureInfo(item.Texture->ArrayIndex, imageW, imageH);
	vertex.OffsetXYZ = VertexOptions::EntityPackXYZ(0, 0);
	vertex.ScaleX = 1;
	vertex.ScaleY = 1;
	vertex.TranslationIndex = (float)colorMapIndex;
	vertex.SpriteOptions = EntityVertex::PackSpriteOptions(EntityOrientation::Hud, options, item.OverrideFuzz, 0);
	vertex.Alpha = alpha;
	vertex.ObjectColor = PackColor(objectColor);
	vertex.AddColor = PackColor(PalEntry(255, item.AddColor.r, item.AddColor.g, item.AddColor.b));
	if (item.HasDyn && !shadow)
	{
		vertex.LightProbeR = item.DynRgb[0];
		vertex.LightProbeG = item.DynRgb[1];
		vertex.LightProbeB = item.DynRgb[2];
		vertex.LightProbeW = 1.0f;
	}
	return vertex;
}

void WeaponRenderer::SetUniforms(EntityProgram& program, Shared::RenderInfo& renderInfo, const HudItem& item, bool material)
{
	const auto& u = renderInfo.Uniforms;

	// The HUD projection (VRMode::GetHUDSpriteProjection): the pixels of the whole screen, like classic DrawEndScene2D. The
	// corners already include viewwindowx / viewwindowy.
	VSMatrix hud = VRMode::GetVRMode(true)->GetHUDSpriteProjection();

	program.BoundTexture(BindTextures::BoundTexture);
	program.BrightmapTexture(BindTextures::BrightmapTexture);
	program.ColormapTexture(BindTextures::Colormap);
	program.SectorColormapTexture(BindTextures::SectorColormap);
	program.SectorFogTexture(BindTextures::SectorFog);
	program.SectorGlowTexture(BindTextures::SectorGlow);
	// gl_weaponlight scales the view's extra light for the weapon; a negative value is the fixed colormap index of a light amp.
	int extraLight = u.ExtraLightOrColorMapIndex >= 0 ? (int)(r_viewpoint.extralight * gl_weaponlight) : u.ExtraLightOrColorMapIndex;
	program.ExtraLight(extraLight);
	program.HasInvulnerability(u.DrawInvulnerability);
	program.LightLevelMix(u.Mix);
	program.Mvp(hud);
	program.MvpNoPitch(hud);
	program.FuzzFrac(u.TimeFrac);
	program.TimeFrac(0);
	program.ViewRightNormal(::Helion::Geometry::Vec2F(1, 0));
	program.DistanceOffset(u.DistanceOffset);
	program.ColorMix(u.ColorMix.Global);
	program.FuzzDiv(u.FuzzDiv);
	program.PaletteIndex((int)u.PaletteIndex);
	program.ColorMapIndex(u.ColorMapUniforms.GlobalIndex);
	program.LightMode(u.LightMode);
	program.GammaCorrection(u.GammaCorrection);
	program.ViewPos(renderInfo.Camera->Position);
	program.ScreenBounds(::Helion::Geometry::Vec2I(renderInfo.Viewport.width - 1, renderInfo.Viewport.height - 1));
	program.CheckPlaneClip(false);
	program.UseBrightmaps(u.UseBrightmaps);
	program.UseSectorColor(u.SectorColor);
	program.UseSectorFog(u.SectorFogIndex);
	program.SetSpriteClipDownScaleAmount(1.0f);
	program.ColorClamp(1.0f);
	program.MaxDistanceSquared(FLT_MAX / 4);
	program.FadeDistance(FLT_MAX / 4);
	program.WallClipTexture(BindTextures::WallClipTexture);
	program.PlaneClipTexture(BindTextures::PlaneClipTexture);
	program.MapDataTexture(BindTextures::MapLineData);
	program.LineHeightsTexture(BindTextures::LineHeights);

	program.ViewAngles(::Helion::Geometry::Vec3F(0, 0, 0));
	program.PixelStretch(1.0f);
	program.OrthoView(false);
	program.UseLightProbe(gl_light_sprites);
	program.EnhancedNightvision(gl_enhanced_nightvision);
	const bool translucentTexture = item.GameTexture != nullptr && item.GameTexture->GetTranslucency();
	program.MaskThreshold((translucentTexture || item.OverrideFuzz != 0 || material) ? 0.f : (float)gl_mask_sprite_threshold);
	program.CullColor(FVector4(0, 0, 0, 0));
	program.FuzzTimer((float)(I_msTime() * 0.001));

	HWViewpointUniforms vp = *renderInfo.Viewpoint;
	vp.ViewMatrix.loadIdentity();
	vp.ProjectionMatrix = hud;
	vp.CalcDependencies();
	program.ViewpointUniforms(vp);
}

void WeaponRenderer::DrawSprite(Shared::RenderInfo& renderInfo, Data::RenderData<EntityVertex>* data, const HudItem& item, EntityProgram* program, bool material, bool sorted)
{
	FRenderState* state = screen->RenderState();

	program->Bind();
	SetUniforms(*program, renderInfo, item, material);

	if (material)
	{
		program->Tex(BindTextures::BoundTexture);
		program->TextureMode(SortedTranslucentRenderer::GetTextureMode(item.RenderStyle));
		StreamData stream = {};
		stream.uObjectColor = item.ObjectColor;
		stream.uObjectColor2 = item.ObjectColor;
		stream.uAddColor = item.AddColor;
		stream.uDynLightColor = FVector4(item.DynRgb[0], item.DynRgb[1], item.DynRgb[2], 1.0f);
		stream.timer = (float)(I_msTime() * 0.001);
		program->MaterialStreamData(stream);
	}

	// depth: the HUD is in front of everything; it writes depth only before the OIT passes.
	state->EnableDepthTest(true);
	state->SetDepthFunc(DF_Always);
	state->SetDepthMask(!sorted);
	state->SetCulling(Cull_None);
	if (sorted)
		SortedTranslucentRenderer::SetStyle(*state, item.RenderStyle);

	data->Draw();
	program->Unbind();

	state->SetDepthFunc(DF_Less);
	state->SetDepthMask(!sorted ? true : false);
}

void WeaponRenderer::DrawModel(Shared::RenderInfo& renderInfo, const HudItem& item, bool clearDepth)
{
	FRenderState* state = screen->RenderState();
	AActor* playermo = players[consoleplayer].camera;

	// [BB] The HUD model should be drawn over everything else already drawn.
	if (clearDepth)
		state->Clear(CT_Depth);
	state->EnableDepthTest(true);
	state->SetDepthMask(true);

	EntityModelRenderer::HudModel hud;
	hud.Weapon = item.Weapon;
	hud.Frame = item.ModelFrame;
	hud.Translation = item.ModelTranslation;
	hud.Rotation = item.ModelRotation + FVector3(item.Mx / 4., (item.My - WEAPONTOP) / -4., 0);
	hud.Pivot = item.ModelPivot;
	hud.RenderStyle = item.RenderStyle;
	hud.Alpha = item.Alpha;
	hud.LightLevel = item.LightLevel;
	hud.Fullbright = item.Fullbright;
	hud.NoColoredLight = item.NoColoredLight;
	hud.Desaturation = item.Desaturation;
	hud.LightBufferIndex = item.LightBufferIndex;
	hud.ObjectColor = item.ObjectColor;
	hud.AddColor = item.AddColor;
	hud.PerPixelLights = item.PerPixelLights;
	hud.DynLight[0] = item.DynRgb[0];
	hud.DynLight[1] = item.DynRgb[1];
	hud.DynLight[2] = item.DynRgb[2];
	hud.Camera = playermo;
	hud.TicFrac = Net_ModifyObjectFrac(item.Weapon, r_viewpoint.TicFrac);

	m_modelRenderer->RenderHUDModel(renderInfo, hud);

	state->SetDepthMask(false);
}

void WeaponRenderer::Submit(Shared::RenderInfo& renderInfo, const HudItem& item)
{
	if (item.ModelFrame != nullptr)
	{
		// weapon models are drawn over everything: the first one clears the depth buffer
		const bool clear = !m_depthCleared;
		m_depthCleared = true;
		m_sortedRenderer->Add(-1.0f, [this, item, clear](Shared::RenderInfo& info) { DrawModel(info, item, clear); });
		return;
	}

	if (item.Texture == nullptr)
		return;

	GLLegacyTexture* arrayTexture = item.Texture->ParentArrayTexture != nullptr ? item.Texture->ParentArrayTexture : item.Texture;
	auto& data = m_pool->Get(arrayTexture, item.Brightmap);
	data.ArrayData.Push(MakeVertex(item, nullptr));
	m_used.push_back(&data);

	EntityProgram* program = m_program;
	bool material = false;
	if (m_materialProgramLookup && item.GameTexture != nullptr)
	{
		EntityProgram* materialProgram = m_materialProgramLookup(item.GameTexture->GetShaderIndex());
		if (materialProgram != nullptr)
		{
			program = materialProgram;
			material = true;
		}
	}

	const bool solid = !material && item.RenderStyle.BlendOp != STYLEOP_Shadow && IsSolid(item);
	if (solid)
	{
		DrawSprite(renderInfo, &data, item, program, false, false);
	}
	else
	{
		auto* dataPtr = &data;
		m_sortedRenderer->Add(-2.0f - (float)(m_sortCounter++), [this, dataPtr, item, program, material](Shared::RenderInfo& info)
		{
			DrawSprite(info, dataPtr, item, program, material, true);
		});
	}
}

void WeaponRenderer::Render(Shared::RenderInfo& renderInfo, bool hudModelStep)
{
	Clear();

	AActor* playermo = players[consoleplayer].camera;
	player_t* player = playermo->player;
	const auto& vp = r_viewpoint;
	AActor* camera = vp.camera;

	// this is the same as the software renderer
	if (!player ||
		!r_drawplayersprites ||
		!camera->player ||
		(player->cheats & CF_CHASECAM) ||
		(r_deathcamera && camera->health <= 0))
		return;

	if (m_sortedRenderer == nullptr)
		return;

	sector_t* viewsector = renderInfo.ViewSector != nullptr ? renderInfo.ViewSector : camera->Sector;
	if (viewsector == nullptr)
		return;

	m_fullbright = GetFullbrightState(vp.player);
	m_paletteColorMode = m_textureManager.PaletteColorMode();

	// classic DrawPlayerSprites: the HUD model step draws the items that have a model, the 2D step the others.
	const bool hudModelAvailable = IsHUDModelForPlayerAvailable(camera->player);
	if (hudModelStep && !hudModelAvailable)
		return;

	if (hudModelAvailable)
		PreparePlayerSprites3D(renderInfo, viewsector);
	else
		PreparePlayerSprites2D(renderInfo, viewsector);

	PrepareTargeterSprites(renderInfo, viewsector, vp.TicFrac);

	for (const HudItem& item : m_items)
	{
		if ((item.ModelFrame != nullptr) == hudModelStep)
			Submit(renderInfo, item);
	}
}

}
