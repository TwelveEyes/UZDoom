/*
** helion_contract.fp
**
** The mod-visible shader contract (struct Material, the prototypes, getTexel,
** desaturate, ApplyTextureManipulation, R_DoomLightingEquation, ApplyNormalMap,
** SetMaterialProps, applyFog, ...) for the Helion-style world programs
** (src/rendering/helion/OpenGL/Shader/UzdoomContract.cpp). The shadow and spot
** light attenuation of main.fp are in helion_shadow.fp.
**
** The text is a copy of main.fp (sections between the Material struct and
** SetMaterialProps, plus applyFog / AmbientOcclusionColor) without getLightColor
** and main(): the Helion programs have their own light/fog stage. Keep it in sync
** with main.fp when that changes. The only functional difference is getTexel,
** which samples with the bound array layer when HELION_ARRAY_TEXTURE is defined.
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

struct Material
{
	vec4 Base;
	vec4 Bright;
	vec4 Glow;
	vec3 Normal;
	vec3 Specular;
	float Glossiness;
	float SpecularLevel;
	float Metallic;
	float Roughness;
	float AO;
};

vec4 Process(vec4 color);
vec4 ProcessTexel();
Material ProcessMaterial(); // note that this is deprecated. Use SetupMaterial!
void SetupMaterial(inout Material mat);
vec4 ProcessLight(Material mat, vec4 color);
vec3 ProcessMaterialLight(Material material, vec3 color);

#ifdef USE_GETTEXCOORD
vec2 GetTexCoord()
{
	vec2 texCoord = vTexCoord.st;

	const float pi = 3.14159265358979323846;
	vec2 offset = vec2(0,0);
	#ifdef SHADERTYPE_WARP1
		offset.y = sin(pi * 2.0 * (texCoord.x + timer * 0.125)) * 0.1;
		offset.x = sin(pi * 2.0 * (texCoord.y + timer * 0.125)) * 0.1;

		return texCoord + offset;
	#endif
	#ifdef SHADERTYPE_WARP2
		offset.y = 0.5 + sin(pi * 2.0 * (texCoord.y + timer * 0.61 + 900.0/8192.0)) + sin(pi * 2.0 * (texCoord.x * 2.0 + timer * 0.36 + 300.0/8192.0));
		offset.x = 0.5 + sin(pi * 2.0 * (texCoord.y + timer * 0.49 + 700.0/8192.0)) + sin(pi * 2.0 * (texCoord.x * 2.0 + timer * 0.49 + 1200.0/8192.0));

		return texCoord + offset * 0.025;
	#endif
	return texCoord;
}
#endif


// These get Or'ed into uTextureMode because it only uses its 3 lowermost bits.
const int TEXF_Brightmap = 0x10000;
const int TEXF_Detailmap = 0x20000;
const int TEXF_Glowmap = 0x40000;
const int TEXF_ClampY = 0x80000;

//===========================================================================
//
// RGB to HSV
//
//===========================================================================

vec3 rgb2hsv(vec3 c)
{
	vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
	vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
	vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

	float d = q.x - min(q.w, q.y);
	float e = 1.0e-10;
	return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

//===========================================================================
//
// Color to grayscale
//
//===========================================================================

float grayscale(vec4 color)
{
	return dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
}

//===========================================================================
//
// Desaturate a color
//
//===========================================================================

vec4 dodesaturate(vec4 texel, float factor)
{
	if (factor != 0.0)
	{
		float gray = grayscale(texel);
		return mix (texel, vec4(gray,gray,gray,texel.a), factor);
	}
	else
	{
		return texel;
	}
}

//===========================================================================
//
// Desaturate a color
//
//===========================================================================

vec4 desaturate(vec4 texel)
{
	return dodesaturate(texel, uDesaturationFactor);
}

//===========================================================================
//
// Texture tinting code originally from JFDuke but with a few more options
//
//===========================================================================

const int Tex_Blend_Alpha = 1;
const int Tex_Blend_Screen = 2;
const int Tex_Blend_Overlay = 3;
const int Tex_Blend_Hardlight = 4;

 vec4 ApplyTextureManipulation(vec4 texel, int blendflags)
 {
	// Step 1: desaturate according to the material's desaturation factor.
	texel = dodesaturate(texel, uTextureModulateColor.a);

	// Step 2: Invert if requested
	if ((blendflags & 8) != 0)
	{
		texel.rgb = vec3(1.0 - texel.r, 1.0 - texel.g, 1.0 - texel.b);
	}

	// Step 3: Apply additive color
	texel.rgb += uTextureAddColor.rgb;

	// Step 4: Colorization, including gradient if set.
	texel.rgb *= uTextureModulateColor.rgb;

	// Before applying the blend the value needs to be clamped to [0..1] range.
	texel.rgb = clamp(texel.rgb, 0.0, 1.0);

	// Step 5: Apply a blend. This may just be a translucent overlay or one of the blend modes present in current Build engines.
	if ((blendflags & 7) != 0)
	{
		vec3 tcol = texel.rgb * 255.0;	// * 255.0 to make it easier to reuse the integer math.
		vec4 tint = uTextureBlendColor * 255.0;

		switch (blendflags & 7)
		{
			default:
				tcol.b = tcol.b * (1.0 - uTextureBlendColor.a) + tint.b * uTextureBlendColor.a;
				tcol.g = tcol.g * (1.0 - uTextureBlendColor.a) + tint.g * uTextureBlendColor.a;
				tcol.r = tcol.r * (1.0 - uTextureBlendColor.a) + tint.r * uTextureBlendColor.a;
				break;
			// The following 3 are taken 1:1 from the Build engine
			case Tex_Blend_Screen:
				tcol.b = 255.0 - (((255.0 - tcol.b) * (255.0 - tint.r)) / 256.0);
				tcol.g = 255.0 - (((255.0 - tcol.g) * (255.0 - tint.g)) / 256.0);
				tcol.r = 255.0 - (((255.0 - tcol.r) * (255.0 - tint.b)) / 256.0);
				break;
			case Tex_Blend_Overlay:
				tcol.b = tcol.b < 128.0? (tcol.b * tint.b) / 128.0 : 255.0 - (((255.0 - tcol.b) * (255.0 - tint.b)) / 128.0);
				tcol.g = tcol.g < 128.0? (tcol.g * tint.g) / 128.0 : 255.0 - (((255.0 - tcol.g) * (255.0 - tint.g)) / 128.0);
				tcol.r = tcol.r < 128.0? (tcol.r * tint.r) / 128.0 : 255.0 - (((255.0 - tcol.r) * (255.0 - tint.r)) / 128.0);
				break;
			case Tex_Blend_Hardlight:
				tcol.b = tint.b < 128.0 ? (tcol.b * tint.b) / 128.0 : 255.0 - (((255.0 - tcol.b) * (255.0 - tint.b)) / 128.0);
				tcol.g = tint.g < 128.0 ? (tcol.g * tint.g) / 128.0 : 255.0 - (((255.0 - tcol.g) * (255.0 - tint.g)) / 128.0);
				tcol.r = tint.r < 128.0 ? (tcol.r * tint.r) / 128.0 : 255.0 - (((255.0 - tcol.r) * (255.0 - tint.r)) / 128.0);
				break;
		}
		texel.rgb = tcol / 255.0;
	}
	return texel;
}

//===========================================================================
//
// This function is common for all (non-special-effect) fragment shaders
//
//===========================================================================

vec4 getTexel(vec2 st)
{
#ifdef HELION_ARRAY_TEXTURE
	vec4 texel = texture(tex, vec3(st, boundTextureIndex));
#else
	vec4 texel = texture(tex, st);
#endif

	//
	// Apply texture modes
	//
	switch (uTextureMode & 0xffff)
	{
		case 1:	// TM_STENCIL
			texel.rgb = vec3(1.0,1.0,1.0);
			break;

		case 2:	// TM_OPAQUE
			texel.a = 1.0;
			break;

		case 3:	// TM_INVERSE
			texel = vec4(1.0-texel.r, 1.0-texel.b, 1.0-texel.g, texel.a);
			break;

		case 4:	// TM_ALPHATEXTURE
		{
			float gray = grayscale(texel);
			texel = vec4(1.0, 1.0, 1.0, gray*texel.a);
			break;
		}

		case 5:	// TM_CLAMPY
			if (st.t < 0.0 || st.t > 1.0)
			{
				texel.a = 0.0;
			}
			break;

		case 6: // TM_OPAQUEINVERSE
			texel = vec4(1.0-texel.r, 1.0-texel.b, 1.0-texel.g, 1.0);
			break;

		case 7: //TM_FOGLAYER
			return texel;

	}

	if ((uTextureMode & TEXF_ClampY) != 0)
	{
		if (st.t < 0.0 || st.t > 1.0)
		{
			texel.a = 0.0;
		}
	}

	// Apply the texture modification colors.
	int blendflags = int(uTextureAddColor.a);	// this alpha is unused otherwise
	if (blendflags != 0)
	{
		// only apply the texture manipulation if it contains something.
		texel = ApplyTextureManipulation(texel, blendflags);
	}

	// Apply the Doom64 style material colors on top of everything from the texture modification settings.
	// This may be a bit redundant in terms of features but the data comes from different sources so this is unavoidable.
	texel.rgb += uAddColor.rgb;
	if (uObjectColor2.a == 0.0) texel *= uObjectColor;
	else texel *= mix(uObjectColor, uObjectColor2, gradientdist.z);

	// Last but not least apply the desaturation from the sector's light.
	return desaturate(texel);
}


//===========================================================================
//
// Adjust normal vector according to the normal map
//
//===========================================================================

#if defined(NORMALMAP)
mat3 cotangent_frame(vec3 n, vec3 p, vec2 uv)
{
	// get edge vectors of the pixel triangle
	vec3 dp1 = dFdx(p);
	vec3 dp2 = dFdy(p);
	vec2 duv1 = dFdx(uv);
	vec2 duv2 = dFdy(uv);

	// solve the linear system
	vec3 dp2perp = cross(n, dp2); // cross(dp2, n);
	vec3 dp1perp = cross(dp1, n); // cross(n, dp1);
	vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;

	// construct a scale-invariant frame
	float invmax = inversesqrt(max(dot(t,t), dot(b,b)));
	return mat3(t * invmax, b * invmax, n);
}

vec3 ApplyNormalMap(vec2 texcoord)
{
	#define WITH_NORMALMAP_UNSIGNED
	#define WITH_NORMALMAP_GREEN_UP
	//#define WITH_NORMALMAP_2CHANNEL

	vec3 interpolatedNormal = normalize(vWorldNormal.xyz);

	vec3 map = texture(normaltexture, texcoord).xyz;
	#if defined(WITH_NORMALMAP_UNSIGNED)
	map = map * 255./127. - 128./127.; // Math so "odd" because 0.5 cannot be precisely described in an unsigned format
	#endif
	#if defined(WITH_NORMALMAP_2CHANNEL)
	map.z = sqrt(1 - dot(map.xy, map.xy));
	#endif
	#if defined(WITH_NORMALMAP_GREEN_UP)
	map.y = -map.y;
	#endif

	mat3 tbn = cotangent_frame(interpolatedNormal, pixelpos.xyz, vTexCoord.st);
	vec3 bumpedNormal = normalize(tbn * map);
	return bumpedNormal;
}
#else
vec3 ApplyNormalMap(vec2 texcoord)
{
	return normalize(vWorldNormal.xyz);
}
#endif

//===========================================================================
//
// Sets the common material properties.
//
//===========================================================================

void SetMaterialProps(inout Material material, vec2 texCoord)
{
#ifdef NPOT_EMULATION
	if (uNpotEmulation.y != 0.0)
	{
		float period = floor(texCoord.t / uNpotEmulation.y);
		texCoord.s += uNpotEmulation.x * floor(mod(texCoord.t, uNpotEmulation.y));
		texCoord.t = period + mod(texCoord.t, uNpotEmulation.y);
	}
#endif
	material.Base = getTexel(texCoord.st);
	material.Normal = ApplyNormalMap(texCoord.st);

// OpenGL doesn't care, but Vulkan pukes all over the place if these texture samplings are included in no-texture shaders, even though never called.
#ifndef NO_LAYERS
	if ((uTextureMode & TEXF_Brightmap) != 0)
		material.Bright = desaturate(texture(brighttexture, texCoord.st));

	if ((uTextureMode & TEXF_Detailmap) != 0)
	{
		vec4 Detail = texture(detailtexture, texCoord.st * uDetailParms.xy) * uDetailParms.z;
		material.Base.rgb *= Detail.rgb;
	}

	if ((uTextureMode & TEXF_Glowmap) != 0)
		material.Glow = desaturate(texture(glowtexture, texCoord.st));
#endif
}


//===========================================================================
//
// Applies colored fog
//
//===========================================================================

vec4 applyFog(vec4 frag, float fogfactor)
{
	return vec4(mix(uFogColor.rgb, frag.rgb, fogfactor), frag.a);
}

//===========================================================================
//
// The color of the fragment if it is fully occluded by ambient lighting
//
//===========================================================================

vec3 AmbientOcclusionColor()
{
	float fogdist;
	float fogfactor;

	//
	// calculate fog factor
	//
	if (uFogEnabled == -1)
	{
		fogdist = max(16.0, pixelpos.w);
	}
	else
	{
		fogdist = max(16.0, distance(pixelpos.xyz, uCameraPos.xyz));
	}
	if (uThickFogDistance > 0.0)
	{
		if (fogdist > uThickFogDistance)
		{
			fogdist = fogdist + uThickFogMultiplier * (fogdist - uThickFogDistance);
		}
	}
	fogfactor = exp2 (uFogDensity * fogdist);

	return mix(uFogColor.rgb, vec3(0.0), fogfactor);
}
