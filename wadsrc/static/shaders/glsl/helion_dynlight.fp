/*
** helion_dynlight.fp
**
** The dynamic lights of the Helion-style array texture programs (walls, flats,
** 3D floors, flood fills, decals drawn without a material shader): lightContribution
** and ProcessMaterialLight of material_normal.fp, with the texel colour as the base
** colour and the sector desaturation of the light stage. The lights[] records and
** the cluster lookup (clusterLightIndex) come from the cluster declarations of the
** program (ClusterFragPrelude.cpp), the attenuation functions from helion_shadow.fp.
** Keep it in sync with material_normal.fp when that changes.
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

vec3 HelionLightContribution(int i, vec3 normal)
{
	vec4 lightpos = lights[i];
	vec4 lightcolor = lights[i+1];
	vec4 lightspot1 = lights[i+2];
	vec4 lightspot2 = lights[i+3];

	float lightdistance = distance(lightpos.xyz, pixelpos.xyz);
	if (lightpos.w < lightdistance)
		return vec3(0.0); // Early out lights touching surface but not this fragment

	vec3 lightdir = normalize(lightpos.xyz - pixelpos.xyz);
	float dotprod = dot(normal, lightdir);
	if (dotprod < -0.0001) return vec3(0.0);	// light hits from the backside. This can happen with full sector light lists and must be rejected for all cases. Note that this can cause precision issues.

	float attenuation = clamp((lightpos.w - lightdistance) / lightpos.w, 0.0, 1.0);

	if (lightspot1.w == 1.0)
		attenuation *= spotLightAttenuation(lightpos, lightspot1.xyz, lightspot2.x, lightspot2.y);

	if (lightcolor.a < 0.0) // Sign bit is the attenuated light flag
	{
		attenuation *= clamp(dotprod, 0.0, 1.0);
	}

	if (attenuation > 0.0) // Skip shadow map test if possible
	{
		attenuation *= shadowAttenuation(lightpos, lightcolor.a);
		return lightcolor.rgb * attenuation;
	}
	else
	{
		return vec3(0.0);
	}
}

// The view depth (distance along the view axis) of the lit point when it is not the fragment's (flood fills shade the
// plane behind the drawn quad); <= 0: 1.0 / gl_FragCoord.w.
float helionViewDepth = 0.0;

// base = texel colour, color = the light of the light stage (light level, sector colour, glow, brightmap; at most 1),
// desaturation = the sector desaturation (0..1). Returns the lit colour before fog.
vec3 HelionDynamicLight(vec3 base, vec3 color, float desaturation)
{
	int lightIndex = clusterLightIndex(gl_FragCoord, helionViewDepth > 0.0 ? helionViewDepth : 1.0 / gl_FragCoord.w);
	if (lightIndex < 0)
		return base * color;

	vec3 normal = vWorldNormal.xyz;
	vec3 dynlight = vec3(0.0);
	ivec4 lightRange = ivec4(lights[lightIndex]) + ivec4(lightIndex + 1);
	if (lightRange.z > lightRange.x)
	{
		// modulated lights
		for(int i=lightRange.x; i<lightRange.y; i+=4)
		{
			dynlight += HelionLightContribution(i, normal);
		}

		// subtractive lights
		for(int i=lightRange.y; i<lightRange.z; i+=4)
		{
			dynlight -= HelionLightContribution(i, normal);
		}
	}

	vec3 frag;

	if ( uLightBlendMode == 1 )
	{	// COLOR_CORRECT_CLAMPING
		vec3 lightcolor = color + HelionDesaturate(dynlight, desaturation);
		frag = base * ((lightcolor / max(max(max(lightcolor.r, lightcolor.g), lightcolor.b), 1.4) * 1.4));
	}
	else if ( uLightBlendMode == 2 )
	{	// UNCLAMPED
		frag = base * (color + HelionDesaturate(dynlight, desaturation));
	}
	else
	{
		frag = base * clamp(color + HelionDesaturate(dynlight, desaturation), 0.0, 1.4);
	}

	if (lightRange.w > lightRange.z)
	{
		vec3 addlight = vec3(0.0);

		// additive lights
		for(int i=lightRange.z; i<lightRange.w; i+=4)
		{
			addlight += HelionLightContribution(i, normal);
		}

		frag = clamp(frag + HelionDesaturate(addlight, desaturation), 0.0, 1.0);
	}

	return frag;
}
