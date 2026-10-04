# Levelmesh Rendering — Map

## Destination

A handoff spec at `.scratch/levelmesh-rendering/spec.md` locking every architectural decision for replacing
UZDoom's per-frame BSP-traversal rasterizer with a Helion-style static levelmesh pipeline: z-buffered
whole-level geometry, shader-evaluated per-sector dynamic state, Vulkan-first with an OpenGL 3.3 draw path
also designed (both in the spec), full feature parity with the classic path including portals/sector_link,
the classic path retained as a selectable fallback until the levelmesh path is proven, and a deterministic
A/B rendering acceptance tool. Implementation is a separate, later effort that picks the spec up.

## Notes

- Domain: UZDoom hardware rendering — `src/rendering/hwrenderer/`, `src/common/rendering/{vulkan,gl,hwrenderer}/`.
- Key existing state (verified at charting):
  - `DoomLevelMesh` (`src/rendering/hwrenderer/doom_levelmesh.*`) already builds a whole-level static mesh at
    map load (`maploader.cpp:3251`) — walls (upper/lower/middle, incl. 3D floors), subsector floor/ceiling
    surfaces, planes, sky flags. Raw positions + surface metadata only: no UVs, no lighting, no texture
    bindings. **Its only consumer today is the Vulkan raytracer** (`VkRaytrace`) — keeping the raytracer
    working is a standing constraint.
  - Classic path: `hw_bsp.cpp` per-frame front-to-back BSP traversal + visibility clipping → drawsegs →
    multithreaded render job pool (Flat/Wall/Sprite/Portal jobs) → batches → OpenGL 3.3 (`common/rendering/gl`)
    or Vulkan (`common/rendering/vulkan`) backends.
  - The software renderer and GLES backend were already removed (Oct 2026); backends are GL33 + Vulkan only.
- Skills for every session on this map: research, grilling, domain-modeling. Use CodeGraph tools
  (codegraph_search/explore/callers) for code navigation; grep only for literal-text matters.
- Standing preferences: build with `cmake --build build --config RelWithDebInfo --parallel 3`; there is no unit
  test suite — verification is clean build + in-game testing; git operations via the git MCP; tabs for
  indentation.
- Constraint: `CONTRIBUTING.md` prohibits AI-generated code contributions to upstream ZDoom/GZDoom — this is
  fork work. Flag this to the user before preparing any commit/PR.
- Research findings live in `.scratch/levelmesh-rendering/research/<ticket-slug>.md` (the effort workspace),
  not throwaway `research/<name>` branches: three research subagents run in parallel against one shared
  working tree, where branch checkout would contend; `.scratch` is this repo's established scratch/tracker
  space.
- Tracker files are currently uncommitted in git; the user reviews the map before committing.
- Research phase complete (2026-10-03): tickets 01, 02, 03 resolved; findings in
  `.scratch/levelmesh-rendering/research/`. The frontier is now ticket 04 (HITL grilling). One new ticket
  (11) graduated from ticket 02's findings.

## Decisions so far

(Recorded here as tickets resolve; charting-round decisions below are locked but ticket-less.)

- **Destination = handoff spec, not execution.** The map ends at the spec; a later effort implements it. (Charting, 2026-10-03)
- **Backend: Vulkan-first; the spec designs both GL33 and Vulkan draw paths.** The mesh/sector-state data layer must be backend-neutral. (Charting, 2026-10-03)
- **Full parity with the classic path, including PORTAL lines and sector_link.** See ADR `docs/adr/0001-levelmesh-full-parity.md`. (Charting, 2026-10-03)
- **Classic BSP rasterizer stays, selectable; levelmesh becomes the default once proven.** (Charting, 2026-10-03)
- **Sprites stay on the existing batch path**, drawn in the same pass z-buffered against the levelmesh; per-sprite frustum tests replace traversal-based clipping/sorting. (Charting, 2026-10-03)
- **Dynamic sectors: per-sector state buffer + vertex-shader warp.** Doom topology is static — only heights, planes, textures, and light change at runtime — so no runtime re-triangulation. Exact mechanism locked in ticket 05. (Charting, 2026-10-03)
- **Acceptance bar includes a deterministic A/B rendering tool** (both paths, same map+camera, pixel-diff) — designed in the spec, built by the implementation effort. (Charting, 2026-10-03)
- **01 — Helion levelmesh deep-dive (resolved, research).** Helion bakes two per-texture VBOs at load, split by a per-plane `SectorDynamic` bitmask: a static VBO updated in place via sub-uploads, and a dynamic VBO carrying current *and* previous-frame Z/UV that the GPU interpolates by tick fraction; light changes never touch vertices — they write a per-sector GPU light buffer sampled per-vertex; sprites are one GL point expanded to a billboard by a geometry shader (distance+FOV cull, no LOS ray); portals/sector_link are a flood-fill + fake-wall + stencil system; draws batch per texture, so per-frame CPU scales with moving sectors + visible sprites, not level size. Findings: `research/01-helion-levelmesh.md` (every claim cited to a Helion source file).
- **02 — Consumer inventory (resolved, research).** The "renderer traversal is render-only" assumption is **broken** in two places: (a) per-frame flags `ML_MAPPED`/`SSECMF_DRAWN` are read by the automap and serialized into savegames — the levelmesh path needs a per-frame exposure pass; (b) dither transparency is a per-frame feedback loop (visible actors → `P_CheckSight` → `WALLF_DITHERTRANS` traces) — the levelmesh needs a visible-actor hook. Everything else (walls/flats/sprites/fake flats/glow/lightmaps/sky/job pool) replaces cleanly; VkRaytrace is purely a data-model constraint; the portal pipeline (stencil + `SSRF_SEEN` coverage) is a hook feeding 06. Graduated into ticket 11. Findings: `research/02-consumer-inventory.md` (22-row table, file:line verified).
- **03 — Classic portal flow (resolved, research).** No geometry is ever transformed or re-meshed: a portal re-runs the identical traversal in a child drawinfo with a **remapped viewpoint** (line portals: rotation+translation+angle diff+z rebase; sector_link: displacement add), rendered LIFO after the opaque pass with a stencil as the only pixel-level region separator over shared depth (cleared/restored per portal polygon). Hardest parity items: the sector_link coverage handshake (`SSRF_SEEN`/`UnclipSubsector`), the full stencil/depth contract, per-frame portal registration. Design consequence: target one shared mechanism — per-region meshes + view transforms — not per-portal mesh baking. Findings: `research/03-portal-flow.md` (139 file:line citations).

## Not yet specified

- **VkRaytrace adaptation**: purely a data-model constraint (02 confirmed zero per-frame visibility coupling;
  it rebuilds only on mesh-pointer change, `vk_raytrace.cpp:49-60`) — graduates from "Levelmesh geometry
  data model" (04).
- **Slaughter-scale memory/perf**: vertex counts on huge levels, 32-bit indexing, buffer streaming budgets —
  graduates from 04 / "Draw organization" (08); Helion's per-texture VBO batching is the reference layout (01).
- **Fake flats in the mesh world**: extra flat surface in the mesh vs a second pass — graduates from 04 / 06 /
  07 (classic contract documented in 03; Helion's flood-fill approach in 01).
- **Sky and sky portals in the mesh world** (`hw_sky.cpp`, `hw_skyportal.cpp`) — graduates from 06 or 07
  (classic stencil/depth contract documented in 03).
- **Selection mechanism UI**: cvar vs ZWidget settings entry for switching paths — part of 08.
- **Region model for portals** — 03 shows the classic path never transforms geometry: portals re-traverse with
  a remapped viewpoint in a child drawinfo (LIFO, stencil-separated). The design space for 06 is per-region
  meshes + view transforms, not per-portal mesh baking.

## Out of scope

- Gameplay sight/LOS logic (`P_CheckSight`, visual thinkers) — unchanged unless ticket 02 proves renderer coupling.
- Vulkan raytracer feature work beyond "keep working".
- Upstreaming to ZDoom/GZDoom (CONTRIBUTING.md prohibits AI-generated contributions upstream).
- Software renderer and GLES backend — already removed (context, not scope).
