# Levelmesh implementation — vertical-slice ticket proposal

Chunk 5 of the ticket-breakup pipeline. PROPOSAL ONLY — a human reviews and reshapes. Contract: the four digests, the map's "Decisions so far", ADRs 0001–0004; vocabulary per the spec §2 glossary. 11 slices, each a TRACER BULLET: narrow but a complete path through data/shaders/backends/tooling/verification, verifiable on its own when done.
The A/B tool is wave 1 — every later slice's acceptance is measured with it (zero tolerance outside the frozen known-diff allowlist, ADR 0004). The classic BSP path stays selectable (`gl_uselevelmesh 0`) and the Vulkan raytracer keeps working in every slice; the data layer stays backend-neutral throughout.

## 1. Wave overview

| # | Slice | Wave | Blockers |
|---|---|---|---|
| 01 | A/B capture mode + pixel-diff driver | W1 | 0 |
| 02 | Data layer (FLevelMesh) + raytrace adapter | W2 | 1 (01) |
| 03 | First GL33 draw (static + load-dynamic) | W3 | 1 (02) |
| 04 | Dynamic sector lifecycle (movement, re-bake) | W4 | 1 (03) |
| 05 | 3D floors: band split + 3D-light state | W5 | 1 (04) |
| 06 | Portal / sector_link views + skybox | W6 | 2 (04, 05) |
| 07 | Exposure pass + OOB/ortho views + cullcolor | W5 | 1 (04) |
| 08 | Dither transparency: full port | W6 | 1 (07) |
| 09 | Vulkan draw path (synchronous) | W7 | 2 (06, 08) |
| 10 | Vulkan render worker (async) | W8 | 1 (09) |
| 11 | Integrate: matrix, perf bars, soak, PROVEN | W9 | 2 (10, 08) |

Waves are dependency groups, not concurrency (one agent at a time on this repo). Critical path: 01→02→03→04→06→09→10→11.

## 2. Slices (dependency order)

### 01 — A/B capture mode + pixel-diff driver
**Delivers.** Always-compiled, cvar-gated A/B capture mode (default off): synthetic clock — exactly one tick per rendered frame, interpolation fraction pinned at 0.5, sky scroll/`TexAnim`/FOV from tick time (wall-clock `I_GetTimeFrac()` untrustable) — final composed framebuffer written as per-tick PNGs via the `M_ScreenShot` writer (`m_misc.cpp:664`); external `tools/abtest.py` (Python 3 + Pillow + stdlib) with per-run temp-autoexec config (resolution, `vid_preferbackend`, `gl_uselevelmesh`, gamma 1.0, vsync off, default visual cvars), FRESH LEVEL LOAD PER PATH — exposure/dither are monotonic game state, one live session cannot host both paths — and same-backend pairs only (D8); committed manifest (26 maps, tick lists, the existing 26 synthetic demos) + committed known-diff allowlist schema under `tools/abtest/allowlists/` (z-fighting only, human-reviewed, shrink-only, ADR 0004); zero-tolerance comparator (per-pixel diff, connected-component clusters).
**Verified by.** classic-vs-classic determinism run: one acceptance map × {static tick list, demo replay} × {GL33, Vulkan}, cvar 0 on both fresh launches — zero diff; repeated runs yield byte-identical PNGs.
**Blocked by.** None.
**Acceptance.**
- Capture mode default-off: no behavior/perf change; clean, warning-free build.
- Determinism run zero-diff on all 4 combinations; PNGs byte-identical across repeated runs.
- Driver enforces reload-per-path + same-backend pairing; manifest + allowlist schema committed; the `gl_uselevelmesh` Bool cvar (`CVAR_ARCHIVE|CVAR_GLOBALCONFIG`, default false) is created here — the per-run config hard-depends on it; path selection lands in 03.

### 02 — Data layer (FLevelMesh) + raytrace adapter
**Delivers.** The backend-neutral static model at the `maploader.cpp:3251` build site: whole-level 32-byte vertex pool (dual-purpose slot: static `(z,v)` / dynamic `(vparam,0)`), per-region IBOs ordered per region then per piece with the per-(region, texture, bandIndex) sub-range table (texture-sorted, precomputed world AABBs), 64-byte surface records (all statics incl. 07's wall-shading block, baked fake-contrast rel pair, sky flag, band refs), region records (transform slot, `frontFace`, scissor/stencil), region partition via build-time global flood-fill (main region = whole level on portal-free maps), fake flats / 3D-floor surfaces referencing their model sector, the static per-level band table + repeated IBO indices for walls in 3D volumes, build-time sky resolution (`HWSkyInfo::init` verbatim — MBF line texture, sky2/doublesky, fadecolor rule — statics folded into baked u/v + textureIndex), load-dynamic marking (scroll special / transdoor / alpha → slots written `(vparam,0)`), and the per-region actor list (sprites → their subsector's region, at load). `DoomLevelMesh` demoted: a thin adapter over the existing `hwrenderer::LevelMesh` base feeds `VkRaytrace` the same load-time frozen positions+indices — rebuild-on-mesh-pointer-change contract preserved, `VkRaytrace` itself unchanged.
**Verified by.** in-game raytracer check (level renders in raytrace, not the `NullMesh` fallback cube; rebuild on level change), clean build both backends, memory-budget sanity at SOS_Boom MAP32 (pool/IBO/records within §3 budget), and a classic-vs-classic smoke A/B on one map both backends still zero (the build site changed).
**Blocked by.** 01 — the comparator must exist before any levelmesh work is measured; the smoke A/B gates the maploader change.
**Acceptance.**
- Build replaces the `DoomLevelMesh` build at the `maploader.cpp:3251` site; clean, warning-free build on both backends.
- Raytracer renders the level via the adapter; the `SetLevelMesh` rebuild contract holds; `VkRaytrace` untouched.
- SOS_Boom MAP32 pool/IBO/record sizes within the §3 budget; classic-vs-classic smoke A/B zero-diff (classic path unchanged).

### 03 — First GL33 draw (static + load-dynamic)
**Delivers.** `gl_uselevelmesh 1` (path selection at level setup; mid-game toggle applies at next map load; cvar 0 = classic, unchanged) draws a whole non-portal level on GL33: the levelmesh VS — bit-exact port of the classic per-surface light chain (§6 port list items 1–9: effective light → `RescaleLightLevel` → `CalcRelLight` → `CalcLightLevel` → `CalcLightColor` → fog/cullcolor/glow; int32 on integer paths, C++ op order in float32 on float paths) + VS warp of load-dynamic surfaces from the sector state buffer + sky (`u += levelSkyPos[skyScrollKind]`, fadecolor rule); the `main.fp` adaptation (per-draw light/fog/glow/desat uniforms promoted to per-vertex varyings); the sector state ring (`HW_MAX_PIPELINE_BUFFERS` slots, 96-byte records FULL-COPIED every frame, ADR 0003; every surface fetches its record for shading — light thinkers mutate static geometry); the GL33 backend (one VBO + one VAO per region, `samplerBuffer` record buffers — GL 3.3 core has no SSBOs); the frame build at the `D_Render` site inside the interpolation window (snapshot pack → normal-view cull walk over the static sub-range table with per-sub-range world-AABB frustum cull → slot fill); per-view sprite gather via the region actor list into the EXISTING sprite batch path; pass order 1:1 with classic `RenderScene` (sub-ranges → models → decals → SSAO → TRANSLUCENTBORDER/TRANSLUCENT `DrawSorted`, blended level surfaces fed as per-surface entries into the classic quadtree plane sort).
**Verified by.** A/B GL33 pair on a curated static subset (scanner-confirmed: no portals, no 3D floors, no scroll/transdoor/alpha sectors — candidates DOOM2 MAP01, MAP27) with static tick lists restricted to non-movement frames — zero tolerance, first bring-up records + human review of diff clusters into the allowlist; in-game walk with cvar 1 (sky scrolls, fog, light, sprites/models/decals present); classic unchanged at cvar 0.
**Blocked by.** 02 — consumes the pool, IBOs, surface records, regions, record layouts.
**Acceptance.**
- Curation set A/B GL33 zero-tolerance; diff clusters recorded + human-reviewed into the committed allowlist.
- VS emits all `main.vp` varyings + the promoted per-surface values; light chain formula-for-formula (C++ stays the source of truth); every surface fetches the sector state record for shading.
- Frame build runs inside the interpolation window; per-frame full copy byte-identical across runs (ADR 0003); frame-build window added to the existing `r_perflog` (`hw_clock.cpp:227` — extended, not created); 3D-light state buffer (12 B/light) packed into the frame record (consumed in 05).

**Note.** Largest slice (~3–4k lines); the shader-half/frame-build-half seam admits no independent verification — keep whole (open question 3).

### 04 — Dynamic sector lifecycle (movement, re-bake)
**Delivers.** The rest of the snapshot pass: per-frame record diff against the cached previous-frame copy (moved / settled; two consecutive unchanged frames = settled); first-movement dynamic flag flip + dual-purpose slot rewrite `(z,v)→(vparam,0)` on every referencing surface; re-bake to static (baked `(z,v)` from the settled final non-interpolated height, flag cleared) only when ALL referenced planes have settled; the write rules — the full copy touches only the current ring slot, rare small writes (re-bake patches, slot rewrites, surface-record flag/texture changes) go to ALL ring slots (≤4 small sub-uploads) so in-flight frames never read half-updated state (ADR 0003 pipeline hazard). Doors, lifts, movers now move on the levelmesh path exactly as the classic bakes them.
**Verified by.** A/B GL33 on door-open / lift-mid-transit ticks (DOOM2 MAP01 doors, MAP07 lift) + demo replay on the current subset — zero tolerance; in-game: a door mid-transit is pixel-identical to classic (ADR 0002 parity by construction); surfaces return to static after settling.
**Blocked by.** 03 — the flip/re-bake writes the surface records and vertex pool 03 draws.
**Acceptance.**
- Door/lift ticks + demo replay A/B zero-tolerance (GL33, current subset).
- Lifecycle: first movement flips flags + rewrites slots; re-bake at settle (all referenced planes); scroll/transdoor/alpha (load-marked) never hit the re-bake path.
- Write rules hold: full copy = current slot only; flip/re-bake/texture-swap sub-uploads = all ring slots.

### 05 — 3D floors: band split + 3D-light state
**Delivers.** Walls in 3D volumes drawn as per-band sub-draws: `uBandIndex` + `uSplitTopPlane`/`uSplitBottomPlane` clip distances (the main.vp:79-82 mechanism), split planes read from the sector state records of the band table's model-sector plane refs; band light = the 3D-light state buffer entry or the wall's own chain (`band.lightRef == -1` → own), colormap merged in classic order (light's `extra_colormap` first, then the wall's own FadeColor/FogDensity); 3D-floor flats lit from the per-level 12 B/light 3D-light state buffer — LIVE `*p_lightlevel` + `extra_colormap`, full-copied per frame — incl. the FF_FOG variant (LightColor := light.FadeColor, white FlatColor). The band table + sub-range IBO structure were built in 02; this slice draws them.
**Verified by.** A/B GL33 on 3D-floor maps (DOOM2 MAP07 3D floors+lift, MAP30 3D floors+scale) — zero tolerance; in-game multi-layer 3D-floor tour with active 3D lights.
**Blocked by.** 04 — the verification maps exercise lifts (moving sectors) and the snapshot pass feeding the dynamic split planes.
**Acceptance.**
- MAP07 + MAP30 A/B GL33 zero-tolerance, incl. moving 3D-floor layers.
- Per-band draws match the classic split: plane refs, light/colormap merge order, early-break semantics.
- 3D-light state buffer carries live values — light thinkers on 3D lights mutate flats per frame, frame-locked in the A/B runs.

### 06 — Portal / sector_link views + skybox
**Delivers.** Region-scoped portal rendering (ADR 0001 — first-class, no shim): per-portal view transforms (line: rotate/translate/angle-diff + z-rebase from CURRENT sector heights via `P_TranslatePortal*`; sector_link: pure displacement; nested views compose parent∘child; mirrored composed transform → `frontFace = CW`); window polygons (small dynamic buffer, one quad per portal: static endpoints + current heights of the two portal-adjacent sectors); GPU occlusion queries (depth-only window-poly draw, one query object per portal, issued after the main-region pass for the PREDICTED N+1 candidate set — slot camera + measured velocity × dt, generous frustum — gating both the portal view's draw AND its depth clear; 1-frame staleness inherent to the design); the locked per-frame sequence (stencil +1 → clear depth inside the window → draw the target region's sub-ranges under the composed view transform + destination-line view clip, stencil-masked → recurse with the classic caps + per-frame visited-set → stencil decrement + depth restore; LIFO order); sprite gather per portal view; the skybox as a portal view carrying per-view inkybox (fog-density ×1.5) + cullcolor uniforms, the sky's own transform composed in.
**Verified by.** A/B GL33 on myhouse 20PAM (60 line portals + 7× sector_link) first, then myhouse MAP01 (410 line portals, 5 skyboxes, 3D floors, fake contrast) — zero tolerance; in-game portal tour; the bounded 1–2 frame appearance delay on fast camera turns is visible and accepted per spec.
**Blocked by.** 04 — portal maps exercise moving sectors; 05 — MAP01 stacks 3D floors (20PAM goes green first; the acceptance bar is MAP01).
**Acceptance.**
- 20PAM + MAP01 A/B GL33 zero-tolerance (allowlist bring-up for these maps, human-reviewed).
- Query protocol: pool sized to portal count at load; predicted N+1 candidate set; gates draw + depth clear; no spurious un-occlude.
- Sequence: caps + visited-set hold (no cycle hangs); mirrored regions draw inverted winding; skybox views carry inkybox ×1.5.

### 07 — Exposure pass + OOB/ortho views + cullcolor
**Delivers.** The §8 exposure pass: per-frame per-view candidate LOS pass in classic view order — candidates from the view's culled draw list, ray-tested from the camera to static fan-vertex test points, monotonic `SSECMF_DRAWN`/`ML_MAPPED` marks (map-exempt segs excluded) feeding the shared automap + savegame code unchanged; the new BSP raycast utility over the glnodes tree (segment-vs-node-seg walk; standard/radar solidness via the existing `hw_CheckClip`/`hw_FakeFlat` at current plane heights; deterministic — static tree + origin + predicate heights); offscreen-view rendering on the levelmesh path with the classic view-mode cull rules — A1 suplex pitch window (pitch ±(20+½FOV), cap 179°), A2 radar gate that culls the DRAW too (deathmatch re-tests all), A3 ortho no-fog viewbox cull, A4 undiscovered-secret gate for draw + exposure; and the `Level->cullcolor` latch written per frame per view from the culled list's segs (frustum-only gate — the classic's occlusion gate is omitted, documented delta D6), consumed by the fog/clear-color rules already in 03's VS.
**Verified by.** flag-array + savegame byte-diff parity on a scripted camera walk (reload per path); A/B automap screenshot after the same walk — zero tolerance (shared 2D pass); secret-gating check; cullcolor check (`r_distance_cull_type 2` on a fade map: fog + view clear color match); radar/ortho behavioral checks (documented static-occluder delta allowed — NOT zero tolerance).
**Blocked by.** 04 — needs the full snapshot pass + main draw; OOB views are a second view class on the same frame build.
**Acceptance.**
- `SSECMF_DRAWN`/`ML_MAPPED` sets identical after the scripted walk (or classified as the approved fan-vertex candidate-approximation delta); savegame flag sections byte-identical; automap screenshot A/B zero-tolerance.
- Secrets neither drawn nor exposed in OOB `!r_radarclipper` views, un-gated in normal views; cullcolor latch semantics (no per-frame reset, `gl_cullcolor` init) + fog/clear consumers match.
- BSP raycast deterministic (same origin + heights → same result across runs); radar/ortho behavioral per §8 acceptance 7/8.

### 08 — Dither transparency: full port
**Delivers.** The §8 dither loop (user-chosen FULL port, gated by off-by-default `r_dithertransparency`): per OOB non-portal view, target collection from 03's visible-sprite list (mapsection + A4 secret gate, eligibility `(MF3_ISMONSTER && !MF_CORPSE) || MF_MISSILE`, per-frame dedup, `thing->subsector` index order = classic traversal order, cap 20) then the classic tail verbatim (`P_CheckSight` from the console player's pawn → `SetDitherTransFlags`); flags live in classic playsim storage (`FLineSide`/`plane_t`/`F3DFloor` — no new GPU state); consumption during the cull walk as dither fragments — the same sub-range index range re-drawn with the `DITHERTRANS` pipeline variant (A6 adds the pipeline-key dimension) — clear-on-consume with `dithertranscount` decremented by the line's per-sub-range segCount (A5's static line→sub-range table built at build: per-sub-range index ranges, segCounts, map-exempt bits, `hasSidedefSegs`); unseen surfaces keep residue; A8 order — the tail is the last playsim-mutating step; nothing after it (same-frame walk, worker) may depend on `validcount`-keyed memos.
**Verified by.** A/B GL33 of the 3D frame with `r_dithertransparency 1` + OOB camera + monsters behind partial walls — zero tolerance; decay test (actor leaves view → wall returns within `dithertranscount` frames, frame-for-frame match); crowded-scene cap-20 test; save/restore round trip with dither active equals the classic's equivalent round trip.
**Blocked by.** 07 — the gate requires OOB views + the cull walk with A5/A7 inputs (visible-sprite list, line→sub-range table).
**Acceptance.**
- Dither 3D-frame A/B zero-tolerance; decay + cap-20 + save/restore checks pass.
- A5 table built at build (per-sub-range ranges + segCounts + map-exempt + `hasSidedefSegs`); A6 `DITHERTRANS` variant draws sub-range index ranges under the adapted fragment.
- A8 order holds: tail before walk, same-frame consumption, no memo reads after the tail.

### 09 — Vulkan draw path (synchronous)
**Delivers.** The same GLSL (levelmesh VS + `main.fp` adaptation, incl. the `DITHERTRANS` variant) on Vulkan: the new `LevelMeshSet` descriptor set (surface records, sector state ring as one large buffer with per-slot selection uniform, 3D-light state, band/glow/sub-range tables) + the existing `HWBufferSet` + the existing per-material texture sets; new pipeline (32-byte vertex format, 6 attributes) — purely additive, no pipeline-cache invalidation of classic/raytracer/postprocess; `samplerBuffer` record buffers (native); `VK_QUERY_OCCLUSION` pool (pass-count, not binary — useful for A/B tooling/debug) + the window-poly buffer; the frame build UNCHANGED (game thread, synchronous issue at the `RenderView` site — worker steps collapse, per the GL33 pattern).
**Verified by.** A/B Vulkan pair (parity is per-backend, D8 — same-backend pairs only) on the then-current GL33-green map set — zero tolerance; in-game Vulkan walk on myhouse MAP01 (portals + 3D floors + skybox).
**Blocked by.** 06 and 08 — the Vulkan port must match GL33's final feature coverage (portals/skybox, OOB/dither) or the final per-backend matrix carries a Vulkan hole.
**Acceptance.**
- Vulkan A/B zero-tolerance on the current GL33-green set, incl. portal, 3D-floor, OOB, dither maps.
- LevelMeshSet + HWBufferSet + per-material sets; additive pipeline (classic/raytracer/PP cache keys untouched); one shared GLSL source on both backends.
- `VK_QUERY_OCCLUSION` pool + window-poly buffer match 06's protocol; classic + GL33 paths unchanged.

### 10 — Vulkan render worker (async)
**Delivers.** The spec-level deviation: a Vulkan-only render worker thread owning the 3D world pass only — per-handoff-slot uploads (sector-state slot, 3D-light state, levelSkyPos, window-poly buffer, sprite/model vertex slots), the full 3D issue into the screen's render target incl. occlusion-query issue (the worker computes the predicted N+1 camera from the slot's viewpoints), and a per-slot completion fence; the game thread keeps sim, frame build (inside the interpolation window), the 2D stack, and presentation; in-order slot processing, fence-gated back-pressure, NO frame skipping; the worker consumes frozen slot contents — never playsim state; the fence wait lands at today's `RenderView` site; GL33 stays synchronous (no worker, no second GL context).
**Verified by.** A/B Vulkan still zero-tolerance after the worker lands; in-game 1 h+ stability watch (no crash/hang/GPU memory growth); perflog: the worker fence-wait window appears and the game-thread CPU render window shrinks vs the synchronous Vulkan baseline (09).
**Blocked by.** 09 — the worker consumes the same frame record and Vulkan pipelines; it reshapes nothing in the game-thread frame build.
**Acceptance.**
- In-order slots, back-pressure, no skipping; worker reads frozen slots only (never playsim); GL33 path untouched.
- A/B Vulkan zero-tolerance; 1 h+ stability watch clean (no crash/hang/GPU memory growth).
- `r_perflog` worker fence-wait window lands (the GPU 3D window metric for 11's perf protocol).

### 11 — Integrate: matrix, perf bars, soak, PROVEN
**Delivers.** Verification close-out: the full acceptance matrix run (26 maps × {static tick lists, demo replay} × {GL33, Vulkan} = 104 combinations / 208 fresh-level-load launches) + ticket-07's cvar sweep (r_lightmode / gl_fogmode / r_fakecontrast / r_extralight / r_distance_cull_type / gl_weaponlight / lightmaps) + the gamma × glow axis, with the known-diff allowlist frozen (any NEW diff fails; entries removable, never added silently — ADR 0004); the perf protocol (2560×1440 host-native, vsync off, gamma 1.0, 10 s warmup discarded / 60 s window, median + p95 of {total frame, CPU render window, GPU 3D window} via the extended `r_perflog` + `parse_perflog.py`, reference machine pinned) against ticket-10's classic baseline; the 9-item human soak (campaigns, myhouse tours, SOS_Boom 15-min fly, the §8 scripted save/load + automap + secret walk, 15-min deathmatch, light tour, stability watch incl. one 1 h+ session); and the PROVEN declaration — implementer runs + posts, map owner declares — with the `gl_uselevelmesh` default flip (false → true) + SettingsPage entry (next to the backend radio group) shipping WITH it.
**Verified by.** the matrix + sweep all pass (zero tolerance outside the allowlist); perf bars: levelmesh median ≤ classic × 1.05 per map on BOTH backends and SOS_Boom MAP32 ≤ 50% of its classic median; soak passes; no open levelmesh-only bug ≥ S2 (S1 crash/hang/growth; S2 visible artifact on an acceptance map; S3 cosmetic/rare).
**Blocked by.** 10 (the worker is the default-path state the perf bars measure) and 08 (exposure/dither pixel items are part of the A/B pass).
**Acceptance.**
- 104/104 combinations + cvar sweep pass; allowlist frozen, every entry named (z-fight) + human-reviewed, shrink-only.
- Perf bars met on the pinned reference machine; results posted with per-map tables.
- Soak checklist complete; PROVEN declared by the map owner; default flip + SettingsPage entry shipped with the declaration.

## 3. Prefactors

None as separate tickets. The `gl_uselevelmesh` cvar is pulled into 01 (the driver's per-run config hard-depends on it; its §7 "selection" placement is honored — cvar with the tool, path selection with 03). Build-time side structures (per-region actor list, band table, load-dynamic marking, sky resolution) fold into 02's builder — one build order, not mechanical prep.

## 4. Wide-refactor check

None found. The two candidates that could look wide are contained: the `DoomLevelMesh` → adapter demotion touches one consumer chain (`SetLevelMesh` → `VkRaytrace`), and the `RenderView` → frame-build replacement is a single lambda site in the `D_Render` action. No mechanical tree-wide change; no expand–contract sequencing needed.

## 5. Ticket publication note

Recommend a NEW effort directory `.scratch/levelmesh-impl/issues/` numbered 01…11, with its own map whose spec pointer is `.scratch/levelmesh-rendering/spec.md`: the wayfinder effort is closed (11/11 resolved, frontier empty), the map says implementation is a separate effort, and continuing at 12 in the same directory would mix resolved DESIGN tickets with open IMPLEMENTATION tickets in one numbering (the digests' "ticket 04–11" references all mean design tickets). One line: separate effort, separate numbering, one spec pointer. Recommendation only — the human decides.

## 6. Open questions for the human

1. **First-draw backend: GL33-first (recommended)** — synchronous, no worker, reuses `FRenderState`, smallest surface area, tracer bullet lands green fastest; the data layer stays backend-neutral so Vulkan attaches in 09. Alternative: a Vulkan synchronous base right after 03 (driver/descriptor risk earlier) at the cost of a second Vulkan-features slice. Confirm?
2. **Ticket directory** — new `.scratch/levelmesh-impl/` effort (recommended, §5) vs continuing numbering at 12 in `.scratch/levelmesh-rendering/issues/`.
3. **Slice 03 size** — first draw (full light-chain VS + frame build + sprites + GL33 objects, ~3–4k lines) as ONE slice: the shader-half/frame-build-half seam admits no independent verification. Accept as-is, or mandate a two-session stretch?
4. **Sky placement** — sky scroll/fadecolor/MBF resolution rides 03 (sky surfaces exist on every map from tick 1); the skybox (a portal view with inkybox) rides 06. Accept, or cut skybox into its own slice between 06 and 09?
5. **06's acceptance bar** — I bar the portal slice at myhouse MAP01 (needs 05 first — MAP01 stacks 3D floors). If 20PAM alone suffices, 06 unblocks after 04 only and W6 parallelizes with 05. Which bar?
