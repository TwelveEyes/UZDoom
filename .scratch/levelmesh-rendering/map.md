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
- Tracker files live in git (branch `agent-levelmesh`); commit after each resolved ticket.
- Research phase complete (2026-10-03): tickets 01, 02, 03 resolved; findings in
  `.scratch/levelmesh-rendering/research/`. Ticket 04 (geometry data model) resolved 2026-10-03.
  Ticket 05 (dynamic sector state) resolved 2026-10-03. Ticket 06 (portal rendering) resolved 2026-10-04.
  Ticket 07 (lighting & visual effects) resolved 2026-10-04 — 08 unblocks (11 was already unblocked by 05);
  the frontier is now 08 (draw organization). One new ticket (11) graduated from ticket 02's findings.

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
- **05 — Dynamic sector state mechanism (resolved, grilling).** Locked: one 96-byte `SectorState` record per sector (tilted floor/ceiling planes, scroll, per-plane light, colormap, glow, transdoor/sky flags) full-copied **every frame** into a ring of `HW_MAX_PIPELINE_BUFFERS` slots; VS selects slot by uniform. The **snapshot pass** (frame setup, after `DoInterpolations`) packs the buffer and, by per-frame record diffing (the classic `vboheight` mechanism, no playsim hooks), drives 04's lifecycle: first movement flips a surface dynamic, N=2 settled frames trigger re-bake; scroll/transdoor/alpha sectors are marked dynamic at load. Records carry **single current CPU-interpolated** values — UZDoom's `DoInterpolations(I_GetTimeFrac())` already writes tick-blended planes before render, so the warp matches classic per-frame bakes exactly (A/B parity by construction); Helion's dirty-only streaming, prev/current GPU mix, and dynamic VBO are deliberately not adopted. 3D floors/fake flats/sector_link need no special mechanism — surfaces sample the record of the sector their plane refs point at. Exact record layout + update protocol + write rules in `issues/05-dynamic-sector-state.md`.
- **04 — Levelmesh geometry data model (resolved, grilling).** Locked: 32-byte uniform vertex (x,y, dual-purpose 8-byte slot — static `(z,v)` / dynamic `(vparam,0)` — u, lightmap UVs, uint32 surface index); one whole-level vertex pool ordered per region then per piece; per-region index buffers holding per-texture sub-ranges; 48-byte surface records in a CPU-writable SSBO (mutable texture index + dynamic flag; static plane refs incl. 3D-floor model sectors, light ref, region, opening/unpegged/window params). Warp: static surfaces draw baked z; only dynamic-flagged surfaces evaluate z from the sector state buffer in the VS; unpegged/middle/lower windows computed in-shader from the four evaluated planes — no per-frame vertex upload exists. Dynamic lifecycle: flag flip on first movement; re-bake to static at rest when all referenced planes have settled. Fake flats = extra static surfaces referencing `heightsec` model sectors — no second pass. VkRaytrace: unchanged, fed by a small adapter with the same load-time frozen geometry as today's builder (`IsControlSector` is disabled — no control-sector skip). 3D-floor structure is static (line special 160 = `LS_NOP`), so sector movement never rebuilds the pool. 32-bit indexing ruled sufficient (137 GB ceiling vs ~100-160 MB slaughter pool). Region record carries 06's transform + front-face (mirrored regions invert winding) + 08's per-texture sub-range table. Spec-ready layouts in `issues/04-geometry-data-model.md`.
- **07 — Lighting & visual effects (resolved, grilling).** The classic per-surface light chain (effective light → `RescaleLightLevel` → `CalcRelLight` → `CalcLightLevel` → `CalcLightColor` → vColor + softlight/fog/desat/glow) runs **in the levelmesh vertex shader** as a formula-for-formula **bit-exact** GLSL port (int32 on integer paths, C++ op order in float32 on the rest; fake-contrast `rel` baked CPU-side at build — no GPU atan); the fragment shader is the existing `main.fp` with a define-level adaptation promoting the per-draw light/fog/glow/desat values to per-vertex varyings (~12 vec4, inside GL33 limits). **Every surface fetches its sector state record for shading** (light thinkers mutate static geometry too) — 04's "light by all surfaces" made explicit. Record amendments (both user-approved): the 96 B sector record's light slots re-slotted to **raw ingredients** (lightlevel + per-plane light offsets + the 9-byte `FColormap`; the "selfmap" annotation was wrong — HW never reads `selfmap`; 05's `skyOffset` slot reserved); the surface record grown **48 → 64 B** (wall statics: tier, side Light/tierLight, baked rel pair, side light flags, lightmapNum, 3D-light ref, band refs; 04's offset hole normalized). **3D floors:** per-level per-frame light state buffer (12 B/light, live `p_lightlevel` + `extra_colormap`); 3D-floor flats take their light/colormap from it (FF_FOG variant); **walls in a volume become per-band sub-draws** (the classic's split-plane draws, hw_walls.cpp:305-325) — static per-level band table + sub-range granularity (region, texture, **bandIndex**), split planes read from sector records. **Sky = ordinary geometry** (frontsector light + colormap, no special light level): all statics (texture, angle, MBF `skytransfer` line-texture case, doublesky) resolved at build; the only dynamic state is the per-level per-frame `levelSkyPos` triple (hw_sky1/2/mistpos) → **zero per-frame record churn for scrolling skies**; fadecolor rule in the VS; skybox = 06 portal view + inkybox boost per view. Glow: sector sentinels (0 = texture-glow fallback via a per-texture glow table, ~0u = off) + the frontsector's actual tilted planes; walls only (no HW flat glow). Parity notes: `selfmap` SW-only, `uLightIndex=0` for the level pass, m2snf fake-infinite walls fog at 255. Verification: A/B tool, zero tolerance — lighting stress/skybox/lightmap/fog/glow/3D-floor/animated/scrolling/transfer-light/fake-contrast maps + cvar sweep + active light thinkers. Full spec-ready section in `issues/07-lighting-effects.md`.
- **06 — Portal & sector_link rendering (resolved, grilling).** Region-scoped portal views: the level is statically partitioned at build time by a **global flood-fill over the sector graph** (adjacency = two-sided *non-portal* lines; portal lines are barriers; one-sided lines never connect); the player's component = the main region, the rest are portal regions; a portal's target = the region behind its *destination* line (line portals) / its destination sector (sector_link). The main view draws only the main region; each portal view draws its target region (+ nested sub-views); the portal's window wall lives in the main region. No special mechanism for fake flats / 3D floors / sector_link destinations (surfaces reference the model sector per 04 and sit in the subsector's region). Per-portal transform = one shared mechanism (view transform + view clip over the target region's sub-ranges): line portals rotate/translate/angle + z-rebase from *current* heights (runtime — parity holds because `DoInterpolations` blends heights pre-render, 05), sector_link displacement, nesting composes; sky/skybox = the same mechanism (07 owns texturing). Visibility = **GPU occlusion query**, 1-frame-ahead prefetch of a **predicted candidate set** (N+1 camera = current + measured velocity × dt; portals inside the predicted generous frustum); a miss is a bounded 1–2 frame delay (fast camera); the query gates both the draw and the depth-clear. Recursion: classic caps + a per-frame visited-set; main pass first, then LIFO/registration, nesting inside the parent. Full per-frame sequence + rationale in `issues/06-portal-rendering.md`.

## Not yet specified

- **Slaughter-scale perf bar**: frame-time budget and buffer streaming budgets at true scale — graduates from
  08 / 09 (vertex/index/surface memory layout already locked by 04; Helion's per-texture batching is the
  reference layout, 01).
- **Selection mechanism UI**: cvar vs ZWidget settings entry for switching paths — part of 08.

## Out of scope

- Gameplay sight/LOS logic (`P_CheckSight`, visual thinkers) — unchanged unless ticket 02 proves renderer coupling.
- Vulkan raytracer feature work beyond "keep working".
- Upstreaming to ZDoom/GZDoom (CONTRIBUTING.md prohibits AI-generated contributions upstream).
- Software renderer and GLES backend — already removed (context, not scope).
