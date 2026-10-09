/*
** levelmesh_light.glsl
**
** GLSL port of the classic DOOM light chain (formula-for-formula, int32 on
** integer paths, float32 with the exact C++ promotion points on float paths).
** Pure functions only, intended for the levelmesh vertex shader.
**
** ELightMode enum values (src/doomtype.h:52-63):
**	NotSet = -1, LinearStandard = 0, DoomBright = 1, Doom = 2, DoomDark = 3,
**	DoomLegacy = 4, Build = 5, ZDoomSoftware = 8, DoomSoftware = 16
**
**---------------------------------------------------------------------------
**
** Copyright 2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

// distfogtable[2][256] from src/rendering/hwrenderer/scene/hw_lighting.cpp:32,
// flattened row-major: rows 0..255 = row 0, rows 256..511 = row 1.
uniform float[512] uDistFogTable;

// Port of RoundHalfEven at src/utility/m_round.h:52-58
// C++: static_cast<int32_t>(std::llrint(val)) - round-to-nearest, ties to even.
int LM_RoundHalfEven(float v)
{
	// r = the round-up candidate; an exact .5 tie landing on an odd r must go down instead.
	float r = floor(v + 0.5);
	if (fract(v) == 0.5 && mod(r, 2.0) == 1.0) r -= 1.0;
	return int(r);
}

// Port of CalcLightLevel at src/rendering/hwrenderer/scene/hw_lighting.cpp:88-114
// isDarkLightMode (hw_drawinfo.h:368-370): lightmode == Doom(2) || lightmode == DoomDark(3)
// isSoftwareLighting (hw_drawinfo.h:353-355): lightmode == ZDoomSoftware(8) || DoomSoftware(16) || Build(5)
int LM_CalcLightLevel(int lightmode, int lightlevel, int rellight, int weapon, int blendfactor)
{
	int light;

	if (lightlevel <= 0) return 0;

	bool darklightmode = (lightmode == 2 || lightmode == 3) || ((lightmode == 5 || lightmode == 8 || lightmode == 16) && blendfactor > 0);

	// [Nash] 14 June 2026 - disabled pure light level temporarily. Will be fixed after UZDoom v5.0
	if (darklightmode && lightlevel < 192 && weapon == 0)
	{
		if (lightlevel > 100)
		{
			light = LM_RoundHalfEven(192.0 - (192 - lightlevel) * 1.87);
			if (light + rellight < 20)
			{
				light = 20 + (light + rellight - 20) / 5;
			}
			else
			{
				light += rellight;
			}
		}
		else
		{
			light = (lightlevel + rellight) / 5;
		}

	}
	else
	{
		light=lightlevel+rellight;
	}

	// Fake contrast should never turn a positive value into 0.
	return clamp(light, 1, 255);
}

// Port of RescaleLightLevel<true> at src/rendering/hwrenderer/scene/hw_lighting.h:35-47
// r_extralight is passed in from the r_extralight cvar (hw_lighting.h:27).
int LM_RescaleLightLevel(int lightlevel, int r_extralight)
{
	//max is needed for negative r_extralight values
	return max(int((clamp(lightlevel, 0, 255) / 255.0) * (255.0 - float(r_extralight))) + r_extralight, 0);
}

// Port of CalcRelLight at src/rendering/hwrenderer/scene/hw_walls.cpp:2100-2115
// (identical LM_CalcRelLight at src/common/rendering/levelmesh.cpp:82-93)
int LM_CalcRelLight(int lightlevel, int orglightlevel, int rel)
{
	if (orglightlevel >= 253)			// with the software renderer fake contrast won't be visible above this.
	{
		return 0;
	}
	else if (lightlevel - rel > 256)	// the brighter part of fake contrast will be clamped so also clamp the darker part by the same amount for better looks
	{
		return 256 - lightlevel + rel;
	}
	else
	{
		return rel;
	}
}

// Port of CalcLightColor at src/rendering/hwrenderer/scene/hw_lighting.cpp:119-141
// pe is the LightColor as vec3, components 0-255. Result is the int-clamped-to-[0,255]
// color in a vec3 (C++ narrows to uint8_t; the clamp mirrors that narrowing).
// isSoftwareLighting (hw_drawinfo.h:353-355): lightmode == ZDoomSoftware(8) || DoomSoftware(16) || Build(5)
vec3 LM_CalcLightColor(int lightmode, int light, vec3 pe, int blendfactor)
{
	int r,g,b;
	ivec3 p = ivec3(pe);

	if (blendfactor == 0)
	{
		if (lightmode == 5 || lightmode == 8 || lightmode == 16)
		{
			return pe;
		}

		r = p.x * light / 255;
		g = p.y * light / 255;
		b = p.z * light / 255;
	}
	else
	{
		// This is what Legacy does with colored light in 3D volumes. No, it doesn't really make sense...
		// It also doesn't translate well to software style lighting.
		int mixlight = light * (255 - blendfactor);

		r = (mixlight + p.x * blendfactor) / 255;
		g = (mixlight + p.y * blendfactor) / 255;
		b = (mixlight + p.z * blendfactor) / 255;
	}
	return vec3(clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255));
}

// Port of GetFogDensity at src/rendering/hwrenderer/scene/hw_lighting.cpp:159-203
// fogrgb: (fogcolor.d & 0xffffff), the packed 24-bit fog RGB (0 = black fog).
// fog_a: FogColor.a - unused by the C++ body, kept for call-site symmetry.
// level_fogdensity: Level->fogdensity; outsidefogdensity: Level->outsidefogdensity;
// outsidefog_rgb: (Level->info->outsidefog & 0xffffff); outsidefog_a: APART(Level->info->outsidefog).
// no_lightfade: !(Level->flags3 & LEVEL3_NOLIGHTFADE).
float LM_GetFogDensity(int lightmode, int lightlevel, int fogrgb, int fog_a, int sectorfogdensity, int blendfactor, int level_fogdensity, int outsidefogdensity, int outsidefog_rgb, int outsidefog_a, bool no_lightfade)
{
	float density;

	// isSoftwareLighting (hw_drawinfo.h:353-355): lightmode == ZDoomSoftware(8) || DoomSoftware(16) || Build(5)
	if ((lightmode == 5 || lightmode == 8 || lightmode == 16) && blendfactor > 0) lightmode = 2;	// The blendfactor feature does not work with software-style lighting.

	if (lightmode == 4)
	{
		// uses approximations of Legacy's default settings.
		density = level_fogdensity != 0 ? float(level_fogdensity) : 18.0;
	}
	else if (sectorfogdensity != 0)
	{
		// case 1: Sector has an explicit fog density set.
		density = float(sectorfogdensity);
	}
	else if (fogrgb == 0)
	{
		// case 2: black fog
		// isDoomSoftwareLighting (hw_drawinfo.h:363-365): lightmode == ZDoomSoftware(8) || DoomSoftware(16)
		// distfogtable[lightmode != ELightMode::LinearStandard][hw_ClampLight(lightlevel)]
		if (((lightmode != 8 && lightmode != 16) || blendfactor > 0) && no_lightfade)
		{
			density = uDistFogTable[lightmode != 0 ? 256 + clamp(lightlevel, 0, 255) : clamp(lightlevel, 0, 255)];
		}
		else
		{
			density = 0.0;
		}
	}
	else if (outsidefogdensity != 0 && outsidefog_a != 0xff && fogrgb == outsidefog_rgb)
	{
		// case 3. outsidefogdensity has already been set as needed
		density = float(outsidefogdensity);
	}
	else  if (level_fogdensity != 0)
	{
		// case 4: level has fog density set
		density = float(level_fogdensity);
	}
	else if (lightlevel < 248)
	{
		// case 5: use light level
		density = float(clamp(255 - lightlevel, 30, 255));
	}
	else
	{
		density = 0.0;
	}
	return density;
}
