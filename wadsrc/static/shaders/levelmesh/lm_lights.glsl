/*
** lm_lights.glsl
**
** Levelmesh dynamic light lists: every consumer reads one list by owner
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Ticket 19. Every dynamic light has a slot record (four vec4 packed like
** AddLightToList: position / radius, color / shadow index, spot direction /
** type, spot cosines / flags / owner actor). The lights reaching a side or
** a section (the playsim's touch lists) are a list in the pool:
** [portal group, slot, ..., LM_LIGHTLIST_END_WORD]; the owner table holds
** each list's pool offset. A draw names its list by owner (uLightOwner,
** levelmesh surfaces: a flat varying, lm_frag.glsl), or by an inline list
** uploaded into the classic light buffer (big models:
** LevelMesh_InlineLightList).
**
** The list is the classic's reachability; the per-consumer rules are
** applied here: surfaces skip DontLightMap lights and lights behind their
** plane (HWWall / HWFlat::SetupLights), actors apply ShouldLightActor.
** Positions are displaced from the light's portal group to the list's
** (FDynamicLight::PosRelative).
**
** Included by every Vulkan vertex and fragment stage after the bindings
** (vk_shader.cpp), so the material light functions and main.vp can use it.
*/

#ifdef LM_LIGHTLISTS

layout(set = 1, binding = 5, std430) buffer readonly LMLightRecs { vec4 lmLightRecs[]; };	// LM_LIGHTREC_VEC4 per slot
layout(set = 1, binding = 6, std430) buffer readonly LMLightOwners { uint lmLightOwners[]; };
layout(set = 1, binding = 7, std430) buffer readonly LMLightPool { uint lmLightPool[]; };
layout(set = 1, binding = 8, std430) buffer readonly LMLightInfo { uint lmLightInfo[]; };

// A position on the wrong side of a surface's plane by less than this is
// taken to lie on it: the classic decides with the light position against
// the plane (Plane::PointOnSide, HWFlat::SetupLights' height test), the
// shader with the interpolated pixel position, which is off by rounding.
const float LM_PLANE_SIDE_EPSILON = 1.0 / 256.0;

#ifdef LEVELMESH
// Levelmesh flats: the per-cluster refined lists (lightrefine.comp).
layout(set = LM_SET, binding = LMBIND_CLUSTERLIGHTS, std430) buffer readonly LMClusterLightBuffer { uint lmClusterLights[]; };
#endif

const int LM_LIST_POOL = 0;
const int LM_LIST_INLINE = 1;	// lights[], as float bits
const int LM_LIST_CLUSTER = 2;	// lmClusterLights: entries slot | section mask << 24

struct LMLightList
{
	int cur;		// next word, -1 = done
	int src;		// LM_LIST_*
	uint mask;		// LM_LIST_CLUSTER: the reading surface's section bit
	uint group;		// the portal group of the list's owner
};

uint lmListWord(LMLightList list, int index)
{
	if (list.src == LM_LIST_INLINE) return floatBitsToUint(lights[index >> 2][index & 3]);
#ifdef LEVELMESH
	if (list.src == LM_LIST_CLUSTER) return lmClusterLights[index];
#endif
	return lmLightPool[index];
}

// owner >= 0: owner table entry; owner < -1: inline list at float index -(owner + 2).
LMLightList lmListBegin(int owner)
{
	LMLightList list;
	list.cur = -1;
	list.src = LM_LIST_POOL;
	list.mask = 0u;
	list.group = 0u;
	if (owner >= 0)
	{
		uint off = lmLightOwners[owner];
		if (off == LM_LIGHTLIST_END_WORD) return list;
		list.cur = int(off);
	}
	else if (owner < -1)
	{
		list.cur = -(owner + 2);
		list.src = LM_LIST_INLINE;
	}
	else return list;
	list.group = lmListWord(list, list.cur);
	list.cur++;
	return list;
}

#ifdef LEVELMESH
// A levelmesh surface's list: its cluster's refined list when the refine
// produced one (flats only), else its owner's.
LMLightList lmListBeginSurface(int owner, int cluster)
{
	if (owner >= 0 && cluster >= 0)
	{
		uint base = uint(cluster) * LM_CLUSTERLIGHT_WORDS;
		if (lmClusterLights[base] != LM_LIGHTLIST_END_WORD)
		{
			uint section = uint(owner) - lmLightInfo[LMLI_NUMSIDES];
			for (uint k = 0u; k < LM_REFINE_SECTIONS; k++)
			{
				if (lmClusterLights[base + 1u + k] == section)
				{
					LMLightList list;
					list.cur = int(base + LM_CLUSTERLIGHT_ENTRIES);
					list.src = LM_LIST_CLUSTER;
					list.mask = 1u << k;
					list.group = lmClusterLights[base + LM_CLUSTERLIGHT_GROUP];
					return list;
				}
			}
		}
	}
	return lmListBegin(owner);
}
#endif

// The next light of the list the consumer accepts. p / n: the consumer's
// position and (surfaces) geometric normal, GL order.
bool lmListNext(inout LMLightList list, int consumer, int actor, vec3 p, vec3 n, out vec4 lightpos, out vec4 lightcolor, out vec4 lightspot1, out vec4 lightspot2, out uint cls)
{
	while (list.cur >= 0)
	{
		uint slot = lmListWord(list, list.cur);
		if (slot == LM_LIGHTLIST_END_WORD)
		{
			list.cur = -1;
			break;
		}
		list.cur++;
#ifdef LEVELMESH
		if (list.src == LM_LIST_CLUSTER)
		{
			if (((slot >> LM_CLUSTERLIGHT_MASKSHIFT) & list.mask) == 0u) continue;
			slot &= LM_CLUSTERLIGHT_SLOTMASK;
		}
#endif
		int b = int(slot) * LM_LIGHTREC_VEC4;
		lightpos = lmLightRecs[b];
		if (lightpos.w <= 0.0) continue;	// inactive (GetRadius() == 0)
		lightcolor = lmLightRecs[b + 1];
		lightspot1 = lmLightRecs[b + 2];
		lightspot2 = lmLightRecs[b + 3];
		uint flags = floatBitsToUint(lightspot2.z);
		uint lightGroup = flags >> LMLF_GROUPSHIFT;
		if (lightGroup != list.group)
		{
			uint numGroups = lmLightInfo[LMLI_NUMGROUPS_WORD];
			if (lightGroup < numGroups && list.group < numGroups)
			{
				uint o = LMLI_HEADER_WORDS + lmLightInfo[LMLI_NUMFFGROUPS_WORD] + 2u * (lightGroup * numGroups + list.group);
				// The table holds map (x, y); GL order is (x, z, y).
				lightpos.x += uintBitsToFloat(lmLightInfo[o]);
				lightpos.z += uintBitsToFloat(lmLightInfo[o + 1u]);
			}
		}
		if ((consumer & LMLC_ACTOR) == 0)
		{
			if ((flags & LMLF_DONTLIGHTMAP) != 0u) continue;
			if (dot(n, lightpos.xyz - p) < -LM_PLANE_SIDE_EPSILON) continue;
		}
		else
		{
			// FDynamicLight::ShouldLightActor
			uint ownerActor = floatBitsToUint(lightspot2.w);
			if ((flags & LMLF_VISIBLETOPLAYER) == 0u) continue;
			if ((flags & LMLF_DONTLIGHTACTORS) != 0u) continue;
			if ((flags & LMLF_DONTLIGHTSELF) != 0u && ownerActor == uint(actor)) continue;
			if ((flags & LMLF_DONTLIGHTOTHERS) != 0u && ownerActor != uint(actor)) continue;
			if ((consumer & LMLC_FORCEATTEN) != 0) lightcolor.a = -abs(lightcolor.a);
		}
		cls = flags & LMLF_CLASSMASK;
		return true;
	}
	return false;
}

// Nearest shadowmap texel between the light and a point (the classic sprite
// light asks a CPU ray test, IShadowMap::ShadowTest).
float lmPointShadow(vec4 lightpos, vec3 p, float shadowIndexA)
{
	float shadowIndex = abs(shadowIndexA) - 1.0;
	if (shadowIndex >= 1024.0) return 1.0;
	vec2 dir = p.xz - lightpos.xz + 0.01;
	float dist2 = dot(dir, dir);
	if (dist2 < 1.0) return 1.0;
	float u;
	if (abs(dir.y) > abs(dir.x))
	{
		float x = dir.x / dir.y * 0.125;
		u = (dir.y >= 0.0) ? 0.125 + x : (0.50 + 0.125) + x;
	}
	else
	{
		float y = dir.y / dir.x * 0.125;
		u = (dir.x >= 0.0) ? (0.25 + 0.125) - y : (0.75 + 0.125) - y;
	}
	float v = (shadowIndex + 0.5) / 1024.0;
	return step(dist2, textureLod(ShadowMap, vec2(u, v), 0.0).x);
}

// HWDrawInfo::GetDynSpriteLight at point p: the summed color of the list's
// lights the actor accepts (no light probe: the CPU adds it).
vec3 lmPointLight(int owner, int actor, vec3 p)
{
	vec3 sum = vec3(0.0);
	LMLightList list = lmListBegin(owner);
	vec4 lightpos, lightcolor, lightspot1, lightspot2;
	uint cls;
	while (lmListNext(list, LMLC_ACTOR, actor, p, vec3(0.0), lightpos, lightcolor, lightspot1, lightspot2, cls))
	{
		vec3 L = p - lightpos.xyz;
		float dist2 = dot(L, L);
		if (dist2 >= lightpos.w * lightpos.w) continue;
		float dist = sqrt(dist2);
		float frac = 1.0 - dist / lightpos.w;
		if (lightspot1.w == 1.0 && dist > 0.0)
			frac *= smoothstep(lightspot2.y, lightspot2.x, dot(-L / dist, lightspot1.xyz));
		if (frac <= 0.0 || lmPointShadow(lightpos, p, lightcolor.a) <= 0.0) continue;
		vec3 c = lightcolor.rgb;
		// The record carries AddLightToList's additive scale; sprites are lit unscaled.
		if ((floatBitsToUint(lightspot2.z) & LMLF_ADDITIVE) != 0u) c *= 5.0;
		// Subtractive: the record holds length - c; sprites add (length - c) * -1.
		if (cls == 1u) c = -c;
		sum += c * frac;
	}
	return sum;
}

#endif
