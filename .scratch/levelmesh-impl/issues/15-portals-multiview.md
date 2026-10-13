# 15: Portals, mirrors, skyboxes, horizons (multi-view)

**Map:** `../map.md`: Frame model, Clusters ("spatial only; portal topology separate"),
`../research/03-portal-flow.md`.
**Type:** task
**Status:** ready-for-agent
**Blocked by:** 12, 14

## What to build

Portal rendering at full classic semantics, on top of multi-view GPU culling:

- **Views.** Each portal type becomes a view with its own transform, clip plane (`mClipLine` /
  `mClipHeight`, written as clip distances), and stencil level, as the classic `HWPortal` state
  machine does. The types are: line portals, mirrors (inverted winding), stacked sector portals,
  `sector_link`, skybox viewpoints, and horizons.
- **Culling per view.**
  - Frustum per view, with the portal window narrowing the frustum where cheap.
  - Hi-Z per view only if ticket 05 / 08 data says it pays; otherwise frustum-only for portal
    views.
  - Each view has its own persistent visibility bitset.
- **Portal discovery without the BSP walk.** Portal window surfaces come from the bake's side list
  as their own bucket.
  - Candidate portals: CPU frustum test of window quads (cheap, exact, same frame).
  - Gating: the main view's cull writes a per-portal visible flag (any window surface passing Hi-Z /
    depth). Portal views are recorded for all candidates and gated with
    `VK_EXT_conditional_rendering` on that flag.
  - Fallback where the extension is missing: draw every candidate, or use the previous frame's flag
    (DC3).
- **Recursion.** Classic caps (`gl_portals` depth, count) plus a visited set. Window stencil +1,
  depth clear inside the window, nested views, then restore.
- **Skybox / horizon.** Same mechanism with the skybox transform. Apply the inkybox fog boost and
  cullcolor in the resolve pass's per-view block.
- **Ownership.** Remove portal walls from classic ownership. During the transitional period, the
  classic portal path is replaced for levelmesh levels.

## Acceptance

- [ ] myhouse 20PAM (60 line portals + sector_link) A/B passes; then myhouse MAP01 (410 line
      portals, skyboxes, 3D floors): only DC1 / DC2 (DC3 only on the no-extension fallback).
- [ ] A mirror map passes (winding, clip).
- [ ] Per-view GPU timings on the portal-dense vantage logged against the ticket-02 classic numbers.

## Comments
