# UZDoom per-frame visibility consumer inventory

Type: research
Status: resolved
Blocked by:

## Question

Inventory every consumer of the classic path's per-frame visibility results (the `hw_bsp.cpp` traversal,
drawsegs, drawlist/batches in `src/rendering/hwrenderer/scene/` and `src/common/rendering/hwrenderer/`), and
classify each for the levelmesh replacement:

- **Replaces cleanly** — the levelmesh path provides the equivalent natively (z-buffer, per-sprite frustum
  test).
- **Needs a hook** — requires an explicit new mechanism in the levelmesh path (name it).
- **Stays** — lives outside the renderer and is untouched.

Specifically check: walls, flats, sprites, portals, fake flats, glow spots, sector glow/pulse, lightmaps and
dynamic lights, sky and sky portals, the multithreaded render job pool, texture precache (`hw_precache.cpp`),
the Vulkan raytracer (`VkRaytrace`), the automap (`am_*`), and anything in game logic that reads
renderer-side per-frame visibility (`P_CheckSight`, `P_LineOpening`, `p_visualthinker`).

Output: a consumer → classification → note table with file:line references, plus a short list of the
assumptions it validates or breaks (in particular: is renderer traversal render-only?).

Findings: single Markdown file with file:line citations, per the research skill.

## Answer

Full consumer table with file:line citations: `../research/02-consumer-inventory.md`.

- **The "render-only" assumption is BROKEN.** The traversal writes per-frame flags into level data that
  game logic reads: `ML_MAPPED` (set only at `hw_bsp.cpp:439`) and `SSECMF_DRAWN` (`hw_bsp.cpp:358,362,374`)
  are read by the automap (`am_map.cpp:2103, 2240, 2692, 3074, 3359`) and serialized into savegames
  (`p_saveg.cpp:347-440`). The levelmesh path needs a **per-frame exposure pass** that keeps writing both
  flags from its frustum cull set (additively — scripts/UMAPINFO can also set `ML_MAPPED`).
- **Dither transparency is a per-frame feedback loop into game-logic sight** (the only renderer→sight
  call): rendered-actor list `RenderedTargets` (`hw_bsp.cpp:55, 927-934`) → `P_CheckSight` filter
  (`hw_bsp.cpp:1134-1139`) → `SetDitherTransFlags` traces writing `WALLF_DITHERTRANS_*`/`dithertransflag`
  (`hw_drawinfo.cpp:716-837`), consumed next frame by walls/flats (`hw_walls.cpp:85-87`,
  `hw_flats.cpp:311,363`). Needs a hook: visible-actor list from the sprite frustum pass + the same trace
  loop.
- **"Needs a hook" consumers**: portals (per-portal clip/stencil/SSRF_SEEN coverage —
  `hw_portal.cpp:41/73/113/799/817`, `hw_drawinfo.cpp:1091`, `hw_bsp.cpp:230`), automap + savegame fog-of-war
  (exposure pass), dither transparency, and `Level->cullcolor` as fog/clear-color source
  (`hw_bsp.cpp:391` → `hw_setcolor.cpp:108`, `r_utility.cpp:1294`).
- **Replaces cleanly**: walls/flats/sprites/particles (drawlist content), fake flats (→ per-sector state
  buffer), sector glow/pulse + plane glow (state buffer / sprite-pass `CheckSpriteGlow`,
  `p_sectors.cpp:1206`), dynamic-light application (dlist → state buffer), sky/sky portals (z-buffered
  skybox), the render job pool (mechanism reusable for state-update + sprite fill).
- **Stays**: texture precache (level-load only, `p_setup.cpp:226`), lightmaps (baked at map load,
  `maploader.cpp:3235`), dynamic-light *collection* (`a_dynlight.cpp:425,434`) and shadow map, visual
  thinkers (`p_effect.cpp:217`), `P_CheckSight`/`P_LineOpening` (`p_sight.cpp`, `p_maputl.cpp:110` — they
  share only the `validcount` invalidation stamp with the renderer, `hw_bsp.cpp:1108` vs
  `a_lights.cpp:535` etc.).
- **VkRaytrace is untouched by the per-frame path**: it consumes only the static `DoomLevelMesh`
  (`maploader.cpp:3251`, `hw_entrypoint.cpp:108`, `vk_raytrace.cpp:49-60`, bound
  `vk_descriptorset.cpp:109`), rebuilt only when the mesh pointer changes — the standing constraint for
  ticket 04 remains, but the raytracer has no dependency on drawlists/traversal.
- Residual risks flagged: `SECMF_DRAWN` has no reader (inert); mirror/stacked-portal sky reflection cases
  (`hw_bsp.cpp:766` ff.) and the `RFL_NO_CLIP_PLANES` CPU-split fallback are the open corners for
  tickets 06/07/08.
