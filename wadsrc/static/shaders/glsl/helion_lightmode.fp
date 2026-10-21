/*
** helion_lightmode.fp
**
** The gl_lightmode equations of main.fp (R_WallColormap, R_PlaneColormap, R_ZDoomColormap,
** R_DoomColormap, R_DoomLightingEquation) for the Helion-style world programs
** (src/rendering/helion/OpenGL/Shader/UzdoomContract.cpp). Needs the viewpoint block
** (uGlobVis, uPalLightLevels, uCameraPos), uLightFactor / uLightDist and the pixelpos /
** vWorldNormal globals. Copied from main.fp, keep in sync.
**
**---------------------------------------------------------------------------
**
** Copyright 2013-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

//===========================================================================
//
// Vanilla Doom wall colormap equation
//
//===========================================================================
float R_WallColormap(float lightnum, float z, vec3 normal)
{
	// R_ScaleFromGlobalAngle calculation
	float projection = 160.0; // projection depends on SCREENBLOCKS!! 160 is the fullscreen value
	vec2 line_v1 = pixelpos.xz; // in vanilla this is the first curline vertex
	vec2 line_normal = normal.xz;
	float texscale = projection * clamp(dot(normalize(uCameraPos.xz - line_v1), line_normal), 0.0, 1.0) / z;

	float lightz = clamp(16.0 * texscale, 0.0, 47.0);

	// scalelight[lightnum][lightz] lookup
	float startmap = (15.0 - lightnum) * 4.0;
	return startmap - lightz * 0.5;
}

//===========================================================================
//
// Vanilla Doom plane colormap equation
//
//===========================================================================
float R_PlaneColormap(float lightnum, float z)
{
	float lightz = clamp(z / 16.0f, 0.0, 127.0);

	// zlight[lightnum][lightz] lookup
	float startmap = (15.0 - lightnum) * 4.0;
	float scale = 160.0 / (lightz + 1.0);
	return startmap - scale * 0.5;
}

//===========================================================================
//
// zdoom colormap equation
//
//===========================================================================
float R_ZDoomColormap(float light, float z)
{
	float L = light * 255.0;
	float vis = min(uGlobVis / z, 24.0 / 32.0);
	float shade = 2.0 - (L + 12.0) / 128.0;
	float lightscale = shade - vis;
	return lightscale * 31.0;
}

float R_DoomColormap(float light, float z)
{
	if ((uPalLightLevels >> 16) == 16) // gl_lightmode 16
	{
		float lightnum = clamp(light * 15.0, 0.0, 15.0);

		if (dot(vWorldNormal.xyz, vWorldNormal.xyz) > 0.5)
		{
			vec3 normal = normalize(vWorldNormal.xyz);
			return mix(R_WallColormap(lightnum, z, normal), R_PlaneColormap(lightnum, z), abs(normal.y));
		}
		else // vWorldNormal is not set on sprites
		{
			return R_PlaneColormap(lightnum, z);
		}
	}
	else
	{
		return R_ZDoomColormap(light, z);
	}
}

//===========================================================================
//
// Doom software lighting equation
//
//===========================================================================
// UZDoom-only: R_DoomLightingEquation with the Build mode parameters (SetLightParms) as arguments, for the Helion light stage.
float R_DoomLightingEquationEx(float light, float lightFactor, float lightDist)
{
	// z is the depth in view space, positive going into the screen
	float z;
	if (((uPalLightLevels >> 8)  & 0xff) == 2)
	{
		z = distance(pixelpos.xyz, uCameraPos.xyz);
	}
	else
	{
		z = pixelpos.w;
	}

	if ((uPalLightLevels >> 16) == 5) // gl_lightmode 5: Build software lighting emulation.
	{
		// This is a lot more primitive than Doom's lighting...
		float numShades = float(uPalLightLevels & 255);
		float curshade = (1.0 - light) * (numShades - 1.0);
		float visibility = max(uGlobVis * lightFactor * z, 0.0);
		float shade = clamp((curshade + visibility), 0.0, numShades - 1.0);
		return clamp(shade * lightDist, 0.0, 1.0);
	}

	float colormap = R_DoomColormap(light, z);

	if ((uPalLightLevels & 0xff) != 0)
		colormap = floor(colormap) + 0.5;

	// Result is the normalized colormap index (0 bright .. 1 dark)
	return clamp(colormap, 0.0, 31.0) / 32.0;
}

float R_DoomLightingEquation(float light)
{
	return R_DoomLightingEquationEx(light, uLightFactor, uLightDist);
}

//===========================================================================
//
// UZDoom-only: the light stage of the table shaders for the Helion world
// programs, evaluated per fragment from the sector light buffers
// (SectorLightBuffers): hw_lighting.cpp CalcLightLevel / CalcLightColor /
// GetFogDensity, hw_setcolor.cpp SetColor / SetFog / SetShaderLight and the
// light / fog part of main.fp getLightColor and main(). Not carried: the
// Legacy colormap blend factor (FColormap::BlendFactor).
//
//===========================================================================

struct HelionLight
{
	vec3 color;			// vColor after the light level and the black fog, before glow and brightmaps
	vec3 fogColor;		// uFogColor
	float fogFactor;	// fogfactor of main.fp (1 = no fog)
	int fogEnabled;		// uFogEnabled of main.fp
};

bool HelionIsSoftwareLighting(int lightmode)
{
	return lightmode == 5 || lightmode == 8 || lightmode == 16;
}

int HelionDivTrunc(int a, int b)
{
	return int(trunc(float(a) / float(b)));
}

// hw_lighting.cpp CalcLightLevel (no blend factor, not a weapon)
float HelionCalcLightLevel(int lightmode, float lightlevel, float rellight)
{
	int level = int(lightlevel);
	int rel = int(rellight);
	if (level <= 0)
		return 0.0;

	int light;
	if ((lightmode == 2 || lightmode == 3) && level < 192)
	{
		if (level > 100)
		{
			light = int(roundEven(192.0 - float(192 - level) * 1.87));
			if (light + rel < 20)
				light = 20 + HelionDivTrunc(light + rel - 20, 5);
			else
				light += rel;
		}
		else
		{
			light = HelionDivTrunc(level + rel, 5);
		}
	}
	else
	{
		light = level + rel;
	}
	return float(clamp(light, 1, 255));
}

// hw_lighting.cpp GetFogDensity for the codes of SectorLightBuffers::GetUzdoomFog:
// >= 10000 forced fog (r_distance_cull_type / fadetable, even when fullbright) of density code - 10000, >= 0 the density,
// -1 case 5 (light level), <= -2 black fog distfogtable with gl_distfog = -code - 2.
const float HelionForcedFog = 10000.0;
float HelionFogDensity(int lightmode, float code, float lightlevel)
{
	if (code >= HelionForcedFog)
		return code - HelionForcedFog;
	if (code >= 0.0)
		return code;

	int i = clamp(int(lightlevel), 0, 255);
	if (code > -1.5)
		return i < 248 ? float(clamp(255 - i, 30, 255)) : 0.0;

	int distfog = int(-code - 2.0 + 0.5);
	if (lightmode != 0)
	{
		if (i < 128)
			return 6.0 + float((distfog >> 1) + HelionDivTrunc(distfog * (128 - i), 48));
		if (i < 216)
			return (216.0 - float(i)) / (216.0 - 128.0) * float(distfog) / 10.0;
		return 0.0;
	}
	if (i < 164)
		return float((distfog >> 1) + HelionDivTrunc(distfog * (164 - i), 164));
	if (i < 230)
		return float((distfog >> 1) - HelionDivTrunc((distfog >> 1) * (i - 164), 230 - 164));
	return 0.0;
}

vec3 HelionDesaturate(vec3 color, float factor)
{
	float gray = dot(color, vec3(0.2126, 0.7152, 0.0722));
	return mix(color, vec3(gray), factor);
}

// light: x = light level of the surface (sector + side / plane light), y = relative light (fake contrast + extra light)
// lightColor: the sector light colour; fog: rgb fade colour, w density code; fullbright > 0.5: fullbright surface or scene
// level: the level constants (x = MAXDIST of SetShaderLight, y = brightfog, z = r_extralight)
HelionLight HelionGetLight(vec2 light, vec3 lightColor, vec4 fog, float fullbright, vec4 level)
{
	// RescaleLightLevel (r_extralight) of the sector light, before the relative light
	int extralight = int(level.z);
	light.x = float(max(HelionDivTrunc(clamp(int(light.x), 0, 255) * (255 - extralight), 255) + extralight, 0));
	HelionLight result;
	int lightmode = uPalLightLevels >> 16;
	int fogmode = (uPalLightLevels >> 8) & 0xff;
	bool software = HelionIsSoftwareLighting(lightmode);

	float softLight = -1.0;
	float density = 0.0;
	float hwlight = 255.0;
	vec3 color = vec3(1.0);
	if (fullbright > 0.5)
	{
		// SetColor / SetFog with fullbright: white, only the forced fog
		if (software)
			softLight = 1.0;
		if (fog.w >= HelionForcedFog)
			density = fog.w - HelionForcedFog;
	}
	else
	{
		hwlight = HelionCalcLightLevel(lightmode, light.x, light.y);
		// CalcLightColor
		color = software ? lightColor : floor(lightColor * 255.0 * hwlight / 255.0) / 255.0;
		if (software)
			softLight = clamp(light.x + light.y, 0.0, 255.0) / 255.0;
		density = HelionFogDensity(lightmode, fog.w, light.x);
	}

	result.fogColor = fog.rgb;
	result.fogEnabled = 0;
	result.fogFactor = 1.0;
	float fogdist = 0.0;
	float lightfactor = 1.0;
	float lightdist = 0.0;
	if (density != 0.0 && fogmode != 0)
	{
		bool black = all(equal(fog.rgb, vec3(0.0)));
		if (lightmode == 2 && black)
		{
			// SetShaderLight(CalcLightLevel(...), lightlevel)
			float olight = light.x;
			if (hwlight > 0.0)
			{
				float maxdist = level.x;
				if (olight < 96.0)
				{
					lightdist = (maxdist / 2.0) + (olight * maxdist / 96.0 / 2.0);
					olight = 96.0;
				}
				else
				{
					lightdist = maxdist;
				}
				lightfactor = 1.0 + ((olight / hwlight) - 1.0) * 0.75;
				if (lightfactor == 1.0)
					lightdist = 0.0;
			}
		}

		result.fogEnabled = black ? fogmode : -fogmode;

		// Korshun: fullbright fog like in software renderer.
		if (software && level.y > 0.5 && !black)
			softLight = 1.0;

		if (fogmode == 1)
			fogdist = max(16.0, pixelpos.w);
		else
			fogdist = max(16.0, distance(pixelpos.xyz, uCameraPos.xyz));
		if (uThickFogDistance > 0.0 && fogdist > uThickFogDistance)
			fogdist = fogdist + uThickFogMultiplier * (fogdist - uThickFogDistance);
		result.fogFactor = exp2(density * (-1.442692 / 64000.0) * fogdist);
	}

	// getLightColor
	if (softLight >= 0.0)
	{
		// SetFog: gl_lightmode 5 (Build) sets SetLightParms(0.2 * fogdensity, 1 / 31)
		color *= 1.0 - R_DoomLightingEquationEx(softLight, 0.2 * density, 1.0 / 31.0);
	}
	else if (result.fogEnabled > 0)
	{
		// brightening around the player for light mode 2
		if (fogdist < lightdist)
			color *= lightfactor - (fogdist / lightdist) * (lightfactor - 1.0);

		// apply light diminishing through fog equation
		color = mix(vec3(0.0), color, result.fogFactor);
	}

	result.color = color;
	return result;
}

// main.fp applyFog for the coloured fog (uFogEnabled < 0)
vec3 HelionApplyFog(HelionLight light, vec3 color)
{
	if (light.fogEnabled < 0)
		return mix(light.fogColor, color, light.fogFactor);
	return color;
}
