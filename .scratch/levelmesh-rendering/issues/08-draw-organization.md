# Draw organization & pipeline fit (both backends)

Type: grilling
Status: resolved
Blocked by: 05, 06, 07

## Question

Lock per-frame draw organization and pipeline integration for both backends (data layer is backend-neutral;
Vulkan first in phasing):

- Draw granularity: one whole-level draw vs per-sector vs chunked/state-grouped draws; state-change
  minimization strategy.
- Fit into the render thread (`r_thread.cpp`) and the Vulkan renderpass/descriptor setup.
- The GL33 draw path design (VAO layouts, state, what the shared data layer exposes to it).
- What replaces the batch system for level geometry, and what stays for sprites (sprites stay on the batch
  path per charting — drawn z-buffered against the mesh).
- The fate of the multithreaded render job pool and texture precache (`hw_precache.cpp`) — e.g. precache
  from the baked mesh's texture list at build time.
- The selection mechanism that makes levelmesh the default once proven: cvar vs ZWidget settings entry.

End state: a spec-ready pipeline section plus a component-fate table (file → kept/replaced/removed for the
geometry path).

## Answer

Resolved 2026-10-04 (grilling, 4 rounds; every branch below was explicitly chosen/confirmed by the user).
Frame organization is locked: the levelmesh path adds a **Vulkan-only render worker thread** (the user chose
this over the single-threaded model that Helion master itself uses — verified from primary source); the game
thread builds the complete frame record inside the interpolation window into 05's ring slot, and the worker
uploads, records, and submits the entire 3D pass. Draw granularity = (region, texture, band) sub-ranges from
04/07, per-sub-range AABB frustum culled per frame, static texture-sorted tables, no per-frame allocation.
Vulkan: dedicated `LevelMeshSet` descriptor set + new pipeline layout/vertex format/pipeline; GL33: global
VBO + one VAO per region, synchronous on the game thread. Selection: `gl_uselevelmesh` cvar + SettingsPage
entry, default off at handoff. The classic path stays fully intact (traversal, job pool, batch lists) —
component-fate table below.

### Threading (rounds 1+3, locked: async render worker, Vulkan-only, 3D pass only)

Round 1 asked for single-thread confirmation on the premise that `r_thread.cpp` is the software renderer's
span-drawer framework — the ticket's "fit into the render thread" is a false premise (no HW render thread
exists). The user asked how Helion handles it first: **Helion master is also synchronous single-threaded**
(`Client/Client.cs` `Window_MainLoop` from the window frame callback → `RunLogic()` (35 tps ticker) →
`Render()` (command replay, world renderer synchronous) → swap; no render-thread file anywhere in
Client/ or Core/Render/, no frame ring, no render-thread history in RELEASENOTES). The user then **chose to
introduce an async render thread** anyway — an explicit spec-level deviation from both Helion and UZDoom's
current state.

Round 3 locked the scope:

- **The worker owns the 3D world pass only**: per-frame uploads (sector-state ring slot, 3D-light state,
  levelSkyPos, sprite/model vertex slots, window-poly buffer), the full levelmesh 3D issue (main-region
  sub-ranges, models, decals, portal views, sprites, occlusion-query issue) into the screen's render target,
  and a per-slot completion fence.
- **The game thread keeps** the sim, the frame build (inside the interpolation window), the 2D stack
  (ZWidget statusbar, menus, console, VM `CallDraw` — coupled to live game state, not a command list), and
  presentation. The fence wait lands where today's `RenderView` call happens (the `D_Render` lambda,
  d_main.cpp) — 2D composition and `End2DAndUpdate()` are untouched.
- **Vulkan only.** The GL33 levelmesh path issues on the game thread (synchronous, as today). A shared
  second GL context for GL33 async is explicitly out of scope / future work (GL calls are thread-bound to
  the context owner; Vulkan queue submission is thread-safe by design).

**Correctness invariant (the heart of the spec):** everything that reads the single-buffered interpolated
playsim state (sector planes, actor positions, light state, sky positions) runs on the game thread
**between `DoInterpolations` and `RestoreInterpolations`** — the frame build replaces today's `RenderView`
call inside the `D_Render` lambda. The worker consumes frozen ring-slot contents only; it never touches
playsim state.

**What the worker buys (stated honestly):** today's Vulkan submissions are already async, and frame N+1's
CPU build already overlaps frame N's GPU execution via the HW_MAX_PIPELINE_BUFFERS ring. What the worker
removes from the game-thread critical path is the **issue + upload CPU cost**: the per-frame sector-state
ring upload (full per-level copy — e.g. 50k sectors × 96 B ≈ 4.8 MB), sprite/model vertex buffer updates,
light state + levelSkyPos, window-poly buffer, descriptor updates, query issue, and queue submit
(driver-side stalls). It is also the architectural precondition for any deeper async render later (05's slot
ring already has the shape).

### Handoff slot & back-pressure (round 3, locked: in-order, fence-gated, 05's ring reused)

- 05's sector-state ring **is** the handoff ring — no second ring. `HW_MAX_PIPELINE_BUFFERS` slots (2
  desktop / 4 Android — buffers.h:32/36, the same constant the existing per-frame `mBufferPipeline` buffers
  use).
- **Slot contents (the frame record):** sector-state records (05, 96 B/sector), 3D-light state (07, 12
  B/light), levelSkyPos triple (07), per-viewpoint values (camera pos/rot, fov, extralight, fog params —
  07's per-view uniform list), the portal-view list (06: portal index, target region, view transform,
  query-gate flag, skybox/inkybox flag), the **culled draw list** (main-region sub-range entries +
  per-portal-view entries, below), sprite + model vertex slot references (the existing per-frame
  `mBufferPipeline` vertex ring — filled on the game thread as today), and the query results to apply
  (previous frame's, below).
- **Lifecycle:** the game thread builds into the oldest free slot (blocks on the oldest in-flight slot's
  fence if none is free — back-pressure); marks it ready; signals the worker. The worker processes slots
  **in order** (no frame skipping: the game thread 2D-composes per frame in order, so every built frame's
  3D pass must complete — stale-frame drop is impossible in this architecture). The worker waits on the
  slot's GPU-completion fence before reusing it, then uploads + records + submits.
- Concurrency: desktop (2 slots) = 1 in build + 1 in flight — one frame of overlap, the maximum this
  architecture allows. Android (4) = two in flight.

### Per-frame draw list & culling (round 1, locked: per-sub-range AABB frustum cull)

- The **sole draw source** is the static per-region sub-range table (04's per-region IBO with per-texture
  sub-ranges, 07's band refinement → granularity (region, texture, bandIndex)). The table is built at load
  and **sorted by texture** (state-change minimization: one material bind per (region, texture); band
  sub-ranges within a texture group differ only by the `uBandIndex` uniform — no state change).
- **Per-frame CPU walk** (game thread, interpolation window, no per-frame allocation): the main-region
  table is always walked; portal-region tables are walked only for portals whose previous-frame occlusion
  query (06, below) passed. Each sub-range entry carries a precomputed world AABB → per-entry frustum test
  (thousands of AABB tests at slaughter scale ≈ trivial); a region early-outs if its own AABB misses. Output:
  a compact per-slot draw list (sub-range index + vertex count; static bound = total sub-range count).
- **Pass order** (replaces the classic `RenderScene` geometry parts; same target, existing `FRenderState`
  state setters):
  1. Main-region sub-ranges — z-buffered, depth write, DF_Less, STYLE_Source alpha cutoff (classic Parts
     1+2+3 semantics: opaque, masked-cutoff, masked-OFS with the existing depth bias; 07's band-split draws
     live here).
  2. Models (existing `GLDL_MODELS` path with bones — unchanged; z-tested against levelmesh depth).
  3. Decals (existing DF_LEqual path — unchanged).
  4. SSAO (unchanged; screen-space, `gl_ssao_portals` budget).
  5. Portal views (06: stencil +1, depth clear, target-region sub-ranges under the view transform with the
     existing `mClipLine`/`mClipHeight` clip, nested recursion, per-view cull-mode flip for mirrored
     regions).
  6. `GLDL_TRANSLUCENTBORDER` + `GLDL_TRANSLUCENT` `DrawSorted` (depth mask off) — blended level surfaces +
     sprites, below.
  7. Occlusion-query issue for the next frame's predicted candidate set (after the main-region pass,
     depth-only window polys, below).

  **Order-independence argument (why texture-grouped order is parity-safe for pass 1):** opaque +
  alpha-cutoff (STYLE_Source, no blend) geometry is order-independent under the z-buffer — the classic's own
  `gl_sort_textures` re-sorting of those lists (hw_drawinfo.cpp:547-551) is a batching optimization, not a
  visual mechanism. Only the blended pass (6) is order-dependent.

- **Blended level surfaces (parity-critical):** the classic draws `GLDL_TRANSLUCENT` (translucent 3D
  planes, translucent flats, translucent models, sprites) with `DrawSorted` (quadtree plane sort —
  `SortNode`/`FindSortPlane`, hw_drawlist.h:55-117), depth mask off, after portals (`RenderTranslucent`,
  hw_drawinfo.cpp:597-611). **The levelmesh feeds its blended surfaces into that exact list as per-surface
  entries** (a surface's pool span = one draw; these are the small blended subset — at most dozens of
  surfaces per map), so the classic's sort algorithm, ordering semantics, and depth-mask state are reused
  verbatim. No deviation, no parity waiver: A/B (02) gates this at zero tolerance (07).

### Vulkan pipeline & descriptors (round 1, locked: dedicated LevelMeshSet)

- New **pipeline layout** for the levelmesh pipeline = **HWBufferSet** (existing: per-viewpoint/matrix/
  stream UBOs + light/bone SSBOs — binds as today; the light buffer is genuinely used by `main.fp`'s
  dynamic lights, bones unused) + **LevelMeshSet** (new: the levelmesh-specific global buffers, below) +
  the **existing per-material texture set** (FTextureManager's Vulkan texture sets — reused as-is for the
  per-texture bind).
- **LevelMeshSet** (one new descriptor-set layout, one set per level): surface records (64 B/surface,
  04/07), sector-state ring (one large buffer, HW_MAX_PIPELINE_BUFFERS × level slots; slot selected by
  uniform), 3D-light state (12 B/light, 07), static tables (band table 07, per-texture glow table 07,
  sub-range table + region records 04/06/08).
- **New vertex format** (32 B, 6 attributes: pos3, dual-slot2, u, lightmapUV2, surfaceIndex — 04) registered
  via `GetVertexFormat`; **new pipeline** (levelmesh VS + `main.fp` fragment with 07's promoted-uniform
  adaptation block). Purely additive: classic/raytracer/PP layouts untouched → no pipeline-cache
  invalidation; the new pipeline is a new cache key.
- **Record-buffer mechanism (locked by constraint):** **uniform texel buffers (`samplerBuffer`)** for all
  record buffers on **both** backends — GL 3.3 core has no SSBOs (SSBO = 4.3 core / ARB extension; the GL
  backend compiles 330-core shaders, gl_shader.cpp:457-459), while `samplerBuffer` is core since GL 3.2
  and native on Vulkan. One shared GLSL (`texelFetch` at integer texel = byte offset / 16; every record size
  is a 16-byte multiple: 64 B = 4 texels, 96 B = 6, 12 B → 16 B padded). A Vulkan-side SSBO variant is a
  09/10 A/B perf item, not a design split.
- **Render passes:** none new — the levelmesh draws into the existing scene render pass (same
  VkRenderPassKey: depth/stencil, MSAA, draw buffers).

### GL33 draw path (round 2, locked: global VBO + one VAO per region)

- One program (the shared levelmesh VS + `main.fp` adaptation — the same GLSL as Vulkan).
- **Geometry:** the vertex pool as one GL VBO (static; one upload at build; dynamic-surface spans
  re-uploaded on rebake — 05) + **one VAO per region** capturing (VBO, region IBO, 6-attribute layout,
  stride 32). Per draw = VAO bind + `glDrawElements` over the sub-range. Region count is small/static →
  VAO count is trivial.
- **Record buffers:** uniform texel buffer objects per LevelMeshSet buffer (static upload + per-frame
  sub-uploads for sector state / light / sky — game thread, synchronous).
- **State:** existing `FRenderState` (depth test/mask, stencil for portal views, the existing
  `mClipLine`/`mClipHeight` VS uniforms for the 06 view clip, cull-mode flip for mirrored regions); the
  sector-state ring as per-slot buffers (HW_MAX_PIPELINE_BUFFERS), selected by uniform.
- **Threading:** game thread (round 3); a shared second GL context for async is out of scope.

### Sprites, models & the job pool (round 2, locked: per-view all-actor frustum pass; pool stays classic-only)

- **Sprite source (no traversal):** per frame, per active view (main view + occlusion-gated portal views),
  walk that view's **region's actor list** (built at load: actors assigned to their subsector's region —
  06's region partition), frustum-test each sprite against that view's frustum (camera frustum for main;
  the portal's view-transform frustum for portal views — the classic's portal `DrawContents` sprite
  semantics), and feed survivors into the **existing sprite batch path** (HWSprite, `GLDL_TRANSLUCENT`
  entries, `DrawSorted` plane sort, OFS variants — all unchanged; per-sprite vertex fill on the game thread
  into the existing per-frame vertex ring as today, issue per the backend's threading).
- Sprites inside a portal view draw inside that view's stencil/clip, z-buffered against the portal view's
  geometry (06 step 6).
- The visible-sprite list this pass produces is ticket 11's input (the RenderedTargets replacement for the
  dither-transparency loop).
- **Job pool** (`renderPool(1)` + `RenderJobQueue`, `gl_multithread`, hw_bsp.cpp:43-51): kept,
  **classic-path-only**. It exists to parallelize per-frame wall/flat vertex generation
  (WallJob/FlatJob) — which the levelmesh eliminates. The sprite gather is a per-view all-actor walk
  (frustum tests are ~ns each) — single-threaded on the game thread. `gl_multithread` is unchanged.
- **Precache** (`hw_precache.cpp`, `gl_precache`): **unchanged** — `PrecacheLevel` (p_setup.cpp) scans WAD
  sector/side/actor data at level load, already renderer-independent; it warms the same textures the
  levelmesh binds. (The ticket's "fate of precache" question is answered by the code.)

### Occlusion query plumbing (round 2, locked: per-portal query IDs + dynamic window-poly buffer)

06 locked the mechanism (per-portal depth-only window-poly draw with depth-pass queries, main view; frame N
queries gate frame N+1 draws; a miss = bounded 1–2 frame delay). 08 locks the plumbing:

- **Query pool:** per level, one query object per portal (allocated at level load, sized to the level's
  portal count; released at level teardown). Vulkan: a VK_QUERY_OCCLUSION pool (pass count, not binary —
  the count is free and useful for A/B tooling/debug) + per-frame result buffer. GL33: depth-pass query
  objects (depth-only draw with no-op FS, no color write).
- **Window-poly buffer:** small dynamic vertex buffer, one quad per portal (two triangles), updated per
  frame during the portal-transform step — endpoints (static, 06) + **current** heights of both
  portal-adjacent sectors (the same heights 06's transforms read; Vulkan: derived by the worker from the
  slot's sector state + the slot's portal transforms; GL33: game thread).
- **Timing (Vulkan worker):** frame N, after the main-region pass — update the window-poly buffer, issue
  the depth-only draw for the **predicted N+1 candidate set** (portals in the generous frustum of the
  predicted N+1 camera = slot viewpoint + measured velocity × dt, computed by the worker from the slot's
  viewpoints), begin the queries. Frame N+1 start: read frame N's results (complete — no stall; the
  one-frame staleness is inherent to 06's design) and apply the gate flags to the slot's portal-view list.
  GL33: the same sequence on the game thread.
- The query gates both the portal view's draw **and** its depth clear (06).

### Selection (round 1, locked: cvar + settings-page entry)

- `gl_uselevelmesh` (Bool, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, **default false at handoff**). Read at level
  setup → picks the path per map; a mid-game toggle applies at the next map load (same semantics as backend
  switching). The A/B tool (02) drives the same cvar.
- **SettingsPage entry** (the LauncherWindow page — used at startup and as the in-game options menu — next
  to the backend radio group): checkbox/radio bound to the cvar. The page currently writes startup
  defaults via FStartupSelectionInfo; the entry additionally writes the cvar live for in-game use.
- **Default flip** (levelmesh becomes the default) is gated on 09/10 results — that is 09/10's decision,
  not this spec's.

### Locked by code investigation (no decision needed)

- **The ticket's render-thread premise is false:** `r_thread.cpp/h` (src/common/rendering/) is the software
  renderer's span drawer (DrawerThreads, line splitting, memcpy-to-videomemory); it is used by the SW
  renderer only, never by the HW path. It stays for SW and is unrelated to the levelmesh.
- **Frame flow (HW):** `D_Display` (display-paced; `End2DAndUpdate()` = compose + swap) → `D_Render`
  lambda → `DoInterpolations(I_GetTimeFrac())` → `RenderView` → `RestoreInterpolations` — all on the game
  thread today. The levelmesh frame build replaces the `RenderView` call site.
- **The 3D view is a render target** composed by the 2D stack (`screen->BeginFrame` → 3D → 2D HUD/statusbar/
  menu → `End2DAndUpdate`) — why the 2D stays on the game thread (round 3).
- **Mirror = `HWMirrorPortal : HWLinePortal`** (hw_portal.h:231) — a line portal → automatically under 06's
  portal-view mechanism (reflection transform + target-region re-draw). No extra work in 08.
- **Shadowmap** (hw_shadowmap.cpp) = GPU ray tests against the play-sim-owned LevelAABBTree —
  traversal-independent, unchanged on both paths.
- **SSAO** (`gl_ssao_portals`, `AmbientOccludeScene`) — screen-space post-process over the rendered image
  with a per-frame portal budget; no level-geometry dependency, unchanged.
- **`gl_sort_textures`** (hw_drawinfo.cpp:547-551) plane-sorts the PLAIN/MASKED lists — a batching
  optimization on order-independent (no-blend) geometry; the levelmesh's texture-grouped order is
  parity-safe for those passes (argument above).
- **Classic pass inventory (for the 1:1 mapping):** `RenderScene` = PLAIN (alpha ≥ 0) → MASKED (alpha >
  gl_mask_threshold cutoff) → MASKEDOFS (depth bias −1, −128) → models (bones) → decals (STYLE_Translucent,
  DF_LEqual); then SSAO; then portals; then `RenderTranslucent` = TRANSLUCENTBORDER + TRANSLUCENT
  `DrawSorted` (depth mask off).

### Component-fate table (geometry path)

The classic path stays fully intact (selectable, the fallback); "removed" means removed from the levelmesh
path's frame, never deleted from the tree.

| Component | Classic path | Levelmesh path | Notes |
|---|---|---|---|
| `hw_bsp.cpp/h` — HWWalk, CreateScene traversal, job pool | kept | removed from frame | traversal CPU scales with level size — the core cost the levelmesh eliminates |
| `hw_walls.cpp/h` — HWWall, per-draw state/light | kept | removed | light chain → levelmesh VS (07) |
| `hw_flats.cpp/h` — HWFlat | kept | removed | same |
| `hw_vertexbuilder.cpp/h` — per-frame plane vertex generation | kept | replaced by 04's static pool build (build time) + 05's rebake sub-uploads | |
| `hw_drawlist.h` — HWDrawList / GLDL_* | kept | kept for sprites + models + decals + blended level surfaces | levelmesh geometry uses no wall/flat lists; blended surfaces feed in as per-surface entries |
| `hw_drawinfo.cpp/h` — CreateScene/RenderScene/DrawScene | kept | replaced by the levelmesh frame build + 3D pass (new module) | DrawScene's shell shape (portal recursion, SSAO, 2D handoff) is preserved |
| `hw_portal.cpp/h` — HWPortal state machine | kept | replaced by 06's region/portal-view/query mechanism | |
| `hw_clipper.cpp` — CPU BSP clipping | kept | removed (VS clip via the existing mClipLine/mClipHeight uniforms) | |
| `hw_sky.cpp/h` — HWSkyInfo per-frame sky | kept | replaced by build-time resolution + levelSkyPos (07) | |
| `hw_precache.cpp` | kept | kept unchanged | already a WAD scan at load, renderer-independent |
| `hw_shadowmap.cpp`, SSAO, post-process | kept | kept unchanged | traversal/screen-space independent |
| `r_thread.cpp/h` (SW span drawer) | kept (SW renderer) | n/a | unrelated to the HW levelmesh |
| **New** | — | levelmesh draw module (frame build + worker + 3D pass + queries), levelmesh VS + main.fp adaptation, LevelMeshSet/VAO/record-buffer wrappers, `gl_uselevelmesh` cvar + SettingsPage entry | |

### Handoffs to dependent tickets

- **06 (portals):** 08 implements the backend query mechanism 06 deferred (per-portal occlusion-query pool +
  dynamic window-poly buffer, in-order fence-gated under the worker — locked above).
- **04/05:** the sub-range table (region, texture, bandIndex) + AABBs is the draw source; 05's ring is the
  handoff ring (slot contents above).
- **07:** band sub-ranges + uBandIndex, per-view uniform list, levelSkyPos, glow table — consumed by the
  walk/VS above.
- **02 (A/B tool):** drives `gl_uselevelmesh`; parity gates = 07's checklist + this ticket's pass-order and
  blended-surface arguments, zero tolerance.
- **09/10 (verification/baseline):** A/B at zero tolerance; slaughter-scale perf items: per-sub-range cull
  walk, per-frame sector-state upload (≈4.8 MB at 50k sectors), query issue + result read, per-view sprite
  gather, worker overhead (fence waits, submit). The **`gl_uselevelmesh` default-flip decision is 09/10's**.
- **11 (game-logic feedback):** the per-view sprite gather's visible-sprite list replaces the
  RenderedTargets consumption point (dither loop).
- **Glossary (CONTEXT.md):** new terms — "Render worker (levelmesh)", "Handoff slot", "Window polygon"
  (following the 05/06/07 pattern).

### Amendments (post-resolution, ticket 11 — 2026-10-04)

- **A1 — Suplex-pitch-window cull for OOB non-radar views.** OOB views without `r_radarclipper` (or
  `LEVEL3_NOFOGOFWAR`) must cull with the classic's expanded vertical window — pitch ± (20 + ½FOV),
  capped at 179° (classic `hw_drawinfo.cpp:490-496`) — not the exact frustum. Their culled lists are the
  exposure candidate sets (11), so the expanded bounds apply to the list itself, not a side pass.
- **A2 — OOB radar views: per-subsector radar gate in the cull walk.** When `r_radarclipper &&
  !LEVEL3_NOFOGOFWAR && bDoOob`, each candidate subsector fails the walk unless its radar gate passes
  (classic `hw_bsp.cpp:800-868`): already-exposed subsectors pass in non-deathmatch; otherwise the
  subsector is radar-ray-tested (11's BSP raycast, radar solidness, origin = `Viewpoint.OffPos`). A
  failing subsector is **not drawn** (radar-gated rendering, not just exposure). Deathmatch: every
  subsector re-tested per frame.
- **A3 — Ortho no-fog views: viewbox cull.** `bDoOrtho && (!r_radarclipper || LEVEL3_NOFOGOFWAR)`: the
  walk tests subsector bboxes against the 2D viewbox (`ext = 3 · offset · tan(½FOV)`, classic
  `RenderOrthoNoFog`, `hw_bsp.cpp:1057-1075`) instead of the frustum.
- **A4 — Secret render gate.** OOB views without `r_radarclipper` exclude undiscovered-secret sectors'
  subsectors from the culled list (`hw_bsp.cpp:797`) and things inside them from the visible-sprite
  list (`hw_bsp.cpp:637`). Normal views are un-gated (the automap's secret display gating is shared `am_`
  code).
- **A5 — Static line→sub-range table.** At build, per line (and per flat plane / 3D-floor plane): the
  sub-range(s) containing it, the index range per sub-range, and the per-line **segCount within each
  sub-range** (the dither `dithertranscount` decrement unit, 11). Plus per-seg `map-exempt` bit (same-
  sector 2s line with an invalid mid texture) and per-subsector `hasSidedefSegs` bit.
- **A6 — `DITHERTRANS` pipeline-variant dimension.** The levelmesh pipeline key set gains the dither
  bit: dither fragments (11) draw the same sub-range index range with the `EFF_DITHERTRANS` / `main.fp
  #define DITHERTRANS` shader (adapted per 07's promoted-uniform variant).
- **A7 — Dither target source.** The per-view visible-sprite list is the dither target source: filter
  by `CurrentMapSections` + the A4 secret gate + eligibility `(MF3_ISMONSTER && !MF_CORPSE) ||
  MF_MISSILE` + per-frame dedup; sort by `thing->subsector` index (classic traversal order — exact in
  OOB mode, no pruning); cap 20.
- **A8 — Frame-build order per view** (game thread, interpolation window): sprite gather → **dither
  tail** (11: `P_CheckSight` + `SetDitherTransFlags`, OOB non-portal views) → cull walk (dither fragment
  splitting + exposure marking + `cullcolor`, 11) → slot handoff. The tail precedes the walk (same-frame
  dither consumption, classic tail → RenderScene order) and is the last playsim-mutating step; the
  worker never touches `validcount`-keyed memos.

### Decision provenance

- Round 1: threading — single-threaded (the recommendation, matching Helion master per primary-source check:
  `Client/Client.cs` `Window_MainLoop`, `Core/Render/Renderer.cs`) **rejected** — the user asked for the
  Helion comparison first, saw it, and **chose an async render thread** anyway. Culling = **per-sub-range
  AABB frustum cull** (recommended option chosen over whole-region and no-cull). Vulkan fit = **dedicated
  new descriptor set** (chosen over extending HWBufferSet). Selection = **cvar + settings-page entry**
  (chosen over cvar-only and launcher-startup-only).
- Round 2: GL33 fit = **global VBO + one VAO per region** (chosen over IBO rebind and per-region VBO
  copies). Sprites/pool = **per-view all-actor frustum pass, pool stays classic-only** (chosen over pooled
  sprite gather and traversal-for-sprites-only). Queries = **per-portal occlusion query IDs + dynamic
  window-poly buffer** (chosen over binary queries and region-level queries).
- Round 3: worker scope = **3D world pass only** (chosen over entire-presented-frame — the 2D stack cannot
  run off-thread without a large thread-safety refactor — and over stale-frame drop, which is
  architecturally impossible with in-order 2D composition). Backend = **worker on Vulkan only** (chosen
  over a shared second GL context). Ring = **in-order, fence-gated, HW_MAX_PIPELINE_BUFFERS reuse** (chosen
  over a 2× deep dedicated ring).
