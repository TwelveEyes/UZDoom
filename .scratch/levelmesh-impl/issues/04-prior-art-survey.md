# 04: Prior-art survey: VKDoom / GZDoom levelmesh, HWMeshHelper

**Map:** `../map.md`: Data model, Frame model.
**Type:** research
**Status:** ready-for-agent
**Blocked by:** None

## Question

What did dpJudas's VKDoom (archived 2025-08) and the GZDoom/Raze levelmesh work actually do?
Specifically:

- Did VKDoom draw the 3D world from the levelmesh, or use it only for raytraced lightmaps and
  shadows?
- How did it bake geometry? `HWMeshHelper` / `HWWallDispatcher` mesh-helper mode exists in this
  tree but is unused. Where did it come from, and what did it rely on?
- How did it handle dynamic sectors, portals, sky, translucency, and materials?
- What was its performance on large maps, and which problems did it record?

Secondary: is there a public GPU-driven Doom renderer with cluster culling and indirect draws to
learn from? Also summarize `research/01-helion-levelmesh.md` in one paragraph, for contrast.

## Deliverable

`../research/04-prior-art-vkdoom.md`, with primary-source citations (repo paths, commit hashes).
End with a "what to reuse / what to avoid" list for tickets 05–08, and append a one-line pointer to
`../map.md` Decisions log.

## Comments
