/*
** lm_frag.glsl
**
** Levelmesh fragment glue: bindless material layers for the shared main.fp
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Included in place of the classic per-material sampler bindings (set 2 of
** the classic layout is the LevelMeshSet in the levelmesh layout). The
** classic names (tex, texture2, ...) become constructors over the bindless
** texture array, so main.fp and the material functions are unchanged.
** Material layer i is bindless slot (slot + i); a layer the material does
** not have reads the null texture in slot 0, like the classic dummy layers.
**
** vLMMaterial = (base slot, layer count, sampler index, texture-mode flags)
*/

layout(set = LM_SET, binding = LMBIND_SAMPLERS) uniform sampler lmSamplers[NUMSAMPLERS];
layout(set = LM_TEXTURE_SET, binding = 0) uniform texture2D lmBindless[];

layout(location = LM_MATERIAL_LOCATION) flat in uvec4 vLMMaterial;

uint LM_LayerSlot(uint layer)
{
	return layer < vLMMaterial.y ? vLMMaterial.x + layer : 0u;
}

// Sampler: the baked clamp mode, except that a clamped-Y wall whose plane
// moved past the texture span samples with wrap there. The classic clamps Y
// only while the wall's v range lies within [0, 1] (HWWall::
// CheckTexturePosition, evaluated from the current heights), so a wall that
// was clamped at load wraps once a door, lift or crusher stretches it. (The
// reverse, a wall that shrinks into [0, 1] and would now clamp, keeps
// wrapping: identical with nearest filtering, a sub-texel edge difference
// with linear filtering.) A macro: vTexCoord is declared after this include.
// The sampler index is the classic clamp mode, built as clamp bits.
#define LM_SAMPLER (((vLMMaterial.z & LM_SAMPLER_CLAMPY) != 0u && (vTexCoord.y < 0.0 || vTexCoord.y > 1.0)) ? (vLMMaterial.z & ~LM_SAMPLER_CLAMPY) : vLMMaterial.z)

#define LM_LAYER(i) sampler2D(lmBindless[nonuniformEXT(LM_LayerSlot(i))], lmSamplers[nonuniformEXT(LM_SAMPLER)])

#define tex LM_LAYER(0u)
#define texture2 LM_LAYER(1u)
#define texture3 LM_LAYER(2u)
#define texture4 LM_LAYER(3u)
#define texture5 LM_LAYER(4u)
#define texture6 LM_LAYER(5u)
#define texture7 LM_LAYER(6u)
#define texture8 LM_LAYER(7u)
#define texture9 LM_LAYER(8u)
#define texture10 LM_LAYER(9u)
#define texture11 LM_LAYER(10u)
#define texture12 LM_LAYER(11u)

// The push constant carries the render state's texture mode; the material's
// layer flags are per surface.
int LM_TextureMode()
{
	return uTextureMode | int(vLMMaterial.w);
}
#define uTextureMode LM_TextureMode()
