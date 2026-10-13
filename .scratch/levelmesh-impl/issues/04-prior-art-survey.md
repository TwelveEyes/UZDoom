# 04: Prior-art survey: VKDoom / GZDoom levelmesh, HWMeshHelper

**Map:** `../map.md`: Data model, Frame model.
**Type:** research
**Status:** resolved
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

- 2026-10-12 (resolved): findings in `../research/04-prior-art-vkdoom.md`.
  - **User decision:** no VKDoom code or architecture in this effort. The research doc is
    lessons and pitfalls only.
  - The `dpjudas/VkDoom` repo has been deleted. Citations use the full-history mirror
    `nashmuhandes/VkDoom` (dpjudas's last commit is `23a344b08`).
  - VKDoom drew the world from the levelmesh only behind `gl_levelmesh` (default off since
    `3cff0de27`), main view only.
    - Visibility stayed a CPU BSP walk, with one `drawIndexed` per range and no GPU culling.
    - Planes re-baked on the CPU on every change.
    - Translucent, sky-hack and polyobject sides and all portal views fell back to classic.
  - GZDoom's levelmesh feeds raytracing only.
  - `HWMeshHelper` is GZDoom code (Oelckers, `9cd57faec`). UZDoom's copy is identical to GZDoom's,
    not VKDoom's extended one.
  - No public GPU-driven Doom renderer exists. Ironwail (Quake) is the nearest reference.
  - Helion draws its static mesh whole with no culling, and recently added an adaptive BSP
    fallback (#1783) and texture arrays (#1786).
  - **ADR 0003 kept (user, 2026-10-12):** its approach matches VKDoom's, but the code is GZDoom's
    and the update model differs. A provenance note was added to the ADR.
  - **Ticket 05 updated (user, 2026-10-12):** added a no-cull (Helion-style) arm and a
    moving-sector vantage.
