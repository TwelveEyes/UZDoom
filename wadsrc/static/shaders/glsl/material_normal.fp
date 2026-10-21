/*
** material_normal.fp
**
**
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

vec3 lightContribution(vec4 lightpos, vec4 lightcolor, vec4 lightspot1, vec4 lightspot2, vec3 normal)
{
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

vec3 ProcessMaterialLight(Material material, vec3 color)
{
	vec4 dynlight = uDynLightColor + vec4(vDynLight, 0.0);
	vec3 normal = material.Normal;

	if (uLightIndex >= 0)
	{
		ivec4 lightRange = ivec4(lights[uLightIndex]) + ivec4(uLightIndex + 1);
		if (lightRange.z > lightRange.x)
		{
			// modulated lights
			for(int i=lightRange.x; i<lightRange.y; i+=4)
			{
				dynlight.rgb += lightContribution(lights[i], lights[i+1], lights[i+2], lights[i+3], normal);
			}

			// subtractive lights
			for(int i=lightRange.y; i<lightRange.z; i+=4)
			{
				dynlight.rgb -= lightContribution(lights[i], lights[i+1], lights[i+2], lights[i+3], normal);
			}
		}
	}
#ifdef LM_LIGHTLISTS
	else if (uLightOwner != -1 && (uLightConsumer & LMLC_POINT) == 0)
	{
		// The levelmesh light list (lm_lights.glsl): modulated and subtractive lights.
		LMLightList list = lmListBegin(uLightOwner);
		vec4 lightpos, lightcolor, lightspot1, lightspot2;
		uint cls;
		while (lmListNext(list, uLightConsumer, uLightActor, pixelpos.xyz, vWorldNormal.xyz, lightpos, lightcolor, lightspot1, lightspot2, cls))
		{
			if (cls == 0u) dynlight.rgb += lightContribution(lightpos, lightcolor, lightspot1, lightspot2, normal);
			else if (cls == 1u) dynlight.rgb -= lightContribution(lightpos, lightcolor, lightspot1, lightspot2, normal);
		}
	}
#endif

	vec3 frag;

	if ( uLightBlendMode == 1 )
	{	// COLOR_CORRECT_CLAMPING
		vec3 lightcolor = color + desaturate(dynlight).rgb;
		frag = material.Base.rgb * ((lightcolor / max(max(max(lightcolor.r, lightcolor.g), lightcolor.b), 1.4) * 1.4));
	}
	else if ( uLightBlendMode == 2 )
	{	// UNCLAMPED
		frag = material.Base.rgb * (color + desaturate(dynlight).rgb);
	}
	else
	{
		frag = material.Base.rgb * clamp(color + desaturate(dynlight).rgb, 0.0, 1.4);
	}

	if (uLightIndex >= 0)
	{
		ivec4 lightRange = ivec4(lights[uLightIndex]) + ivec4(uLightIndex + 1);
		if (lightRange.w > lightRange.z)
		{
			vec4 addlight = vec4(0.0,0.0,0.0,0.0);

			// additive lights
			for(int i=lightRange.z; i<lightRange.w; i+=4)
			{
				addlight.rgb += lightContribution(lights[i], lights[i+1], lights[i+2], lights[i+3], normal);
			}

			frag = clamp(frag + desaturate(addlight).rgb, 0.0, 1.0);
		}
	}
#ifdef LM_LIGHTLISTS
	else if (uLightOwner != -1 && (uLightConsumer & LMLC_POINT) == 0)
	{
		// Additive lights of the list; the clamp only applies when the list
		// has one, like the classic's non-empty additive range.
		LMLightList list = lmListBegin(uLightOwner);
		vec4 lightpos, lightcolor, lightspot1, lightspot2;
		uint cls;
		bool any = false;
		vec4 addlight = vec4(0.0,0.0,0.0,0.0);
		while (lmListNext(list, uLightConsumer, uLightActor, pixelpos.xyz, vWorldNormal.xyz, lightpos, lightcolor, lightspot1, lightspot2, cls))
		{
			if (cls != 2u) continue;
			addlight.rgb += lightContribution(lightpos, lightcolor, lightspot1, lightspot2, normal);
			// GetLight's list: the plane within the radius (models: every light).
			if ((uLightConsumer & LMLC_ACTOR) != 0 || abs(dot(vWorldNormal.xyz, lightpos.xyz - pixelpos.xyz)) <= lightpos.w) any = true;
		}
		if (any) frag = clamp(frag + desaturate(addlight).rgb, 0.0, 1.0);
	}
#endif

	return frag;
}
