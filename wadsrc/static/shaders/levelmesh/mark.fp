/*
** mark.fp
**
** Levelmesh mark pass: the leaves and lines of the visible surfaces
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Ticket 18. After a view's levelmesh draw, its surfaces are rasterized
** again against the final depth (EQUAL for the drawn buckets, LEQUAL for the
** sky surfaces and exposure quads, which are not in the depth buffer) with
** this fragment stage and no color writes. A fragment that passes sets the
** bits of its surface's leaf and line in the view's region of LMBIND_EXPOSURE
** (uLevelMeshView = its word offset); the game side reads them back next
** frame as the classic's exposure marks (SSECMF_DRAWN, ML_MAPPED) and
** cullcolor latch (HWDrawInfo::ApplyLevelMeshExposure). The bits are
** order-independent, so the result is deterministic.
**
** LM_LEAF_WORDS (the words of the leaf bits, the line bits follow them) is
** defined per level by the renderer; LM_SUBGROUP when the device has
** fragment-stage subgroup ballot / arithmetic (VkLevelMesh::
** FragmentSubgroups).
*/

layout(early_fragment_tests) in;

layout(set = LM_SET, binding = LMBIND_EXPOSURE, std430) buffer LMExposureBuffer { uint lmExposure[]; };

// Large surfaces put thousands of fragments on one word: with subgroup
// operations (LM_SUBGROUP) the lanes of a word OR their masks and one of
// them issues the atomic (a waterfall over the distinct words).
void LM_SetBit(uint word, uint mask)
{
#ifdef LM_SUBGROUP
	for (;;)
	{
		uint first = subgroupBroadcastFirst(word);
		if (word == first)
		{
			uint m = subgroupOr(mask);
			if (subgroupElect() && (lmExposure[word] & m) != m) atomicOr(lmExposure[word], m);
			break;
		}
	}
#else
	if ((lmExposure[word] & mask) == 0u) atomicOr(lmExposure[word], mask);
#endif
}

void main()
{
#ifndef NO_ALPHATEST
	// The masked buckets: the scene draw's alpha test.
	if (texture(tex, vTexCoord.st).a <= uAlphaThreshold) discard;
#endif
	// Helper invocations' writes have no effect: keep them out of the
	// subgroup's election.
	if (gl_HelperInvocation) return;
	uint base = uint(uLevelMeshView);
	uint leaf = vLMExposure.x;
	uint line = vLMExposure.y;
	if (leaf != 0xffffffffu) LM_SetBit(base + (leaf >> 5), 1u << (leaf & 31u));
	if (line != 0xffffffffu) LM_SetBit(base + LM_LEAF_WORDS + (line >> 5), 1u << (line & 31u));
}
