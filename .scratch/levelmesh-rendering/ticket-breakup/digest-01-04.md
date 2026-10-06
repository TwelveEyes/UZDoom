# Digest 01–04 — levelmesh spec §1–§4 (framing, glossary, geometry data model, dynamic sector state)

Grounded against the codebase verified in this task (§5 below). Ticket map: spec §3 ↔ ticket 04, §4 ↔ ticket 05.

## 1. Framing & standing constraints (spec §1)

**Replaces (one line).** The per-frame `hw_bsp.cpp` front-to-back BSP traversal → drawsegs → multithreaded job pool → batches is replaced by a Helion-style static levelmesh: whole-level static mesh built at map load, drawn z-buffered, per-sector dynamic state evaluated in the shaders (sector state buffer + VS warp; no runtime re-triangulation — ADR 0002; full stream, no dirty tracking — ADR 0003).

**Acceptance bar (one line).** Deterministic A/B matrix (26 maps × {static tick lists, demo replay} × {GL33, Vulkan}, both backends, zero tolerance except a frozen known-diff allowlist) + perf bars (levelmesh median ≤ classic × 1.05 per map; SOS_Boom MAP32 ≤ 50 % of classic) + ~half-day human soak → PROVEN (and no open levelmesh-only bug ≥ S2) → `gl_uselevelmesh` default flip + SettingsPage entry ship with the PROVEN declaration (ADRs 0001/0004).

**Standing constraints.**
- Vulkan raytracer must keep working — `DoomLevelMesh`'s only consumer today; geometry work must not break it (fed by a small adapter, ticket 04).
- Classic BSP path stays selectable fallback; `gl_uselevelmesh` is a Bool cvar (`CVAR_ARCHIVE | CVAR_GLOBALCONFIG`), default **false** at handoff; the default flip ships with the PROVEN declaration (tickets 08/09).
- Vulkan-first, but both draw paths (Vulkan and GL 3.3) are specified in this spec.
- The mesh/sector-state data layer is backend-neutral.
- Sprites stay on the existing sprite batch path, drawn in the same pass z-buffered against the levelmesh (ticket 08).
- Backends are GL 3.3 + Vulkan only (software renderer + GLES already removed Oct 2026 — context, not scope).
- Full parity includes `PORTAL` line specials + `sector_link` (ADR 0001): portal rendering is a first-class design problem, not a shim.
- `CONTRIBUTING.md` prohibits upstreaming AI-generated code — fork work; flag before preparing any commit/PR.

**Out of scope (verbatim-short).** Gameplay sight/LOS logic (`P_CheckSight`, visual thinkers) — unchanged; ticket 11 proved renderer coupling and its dither tail *calls* `P_CheckSight`/`SetDitherTransFlags` verbatim (reused, not modified). Vulkan raytracer feature work beyond "keep working". Upstreaming to ZDoom/GZDoom. Software renderer and GLES backend — already removed (context, not scope).

## 2. Glossary (spec §2)

- **Render worker (levelmesh)** — Vulkan-only render worker thread owning the 3D world pass only (per-frame handoff-slot uploads, full 3D issue, occlusion-query issue, per-slot completion fence); consumes frozen slot contents, never playsim state; game thread keeps sim, frame build inside the interpolation window, 2D stack, presentation; no worker on GL33 (ticket 08).
- **Handoff slot** — one slot of the handoff ring, `HW_MAX_PIPELINE_BUFFERS` slots (2 desktop / 4 Android), holding the per-frame frame record: sector-state records, 3D-light state, levelSkyPos, per-viewpoint values, portal-view list, culled draw list, sprite/model vertex slot refs, previous frame's query results; processed in order, fence-gated back-pressure, no frame skipping (ticket 08).
- **Window polygon** — per-portal bounding polygon; small dynamic buffer of one quad per portal (static endpoints + current heights of the two portal-adjacent sectors); stencil/clip-masks the portal's sub-range draws + interior depth clear; depth-only draw target of the per-portal occlusion queries that gate next-frame portal draws (tickets 06/08).
- **Exposure pass** — per-frame per-view candidate LOS pass reproducing the classic automap fog: candidates (the view's culled draw list) ray-tested from camera to static fan-vertex test points by the BSP raycast over the glnodes tree; unoccluded candidates monotonically mark `SSECMF_DRAWN`/`ML_MAPPED`; the per-frame `cullcolor` latch is part of the same pass (ticket 11).
- **Dither fragment** — culled-draw-list append re-drawing an existing sub-range's index range with the `DITHERTRANS` pipeline variant (`EFF_DITHERTRANS` / `main.fp #define DITHERTRANS`), appended during the cull walk when the classic dither flags are set (`dithertranscount` decremented by the line's per-sub-range seg count); clear-on-consume, unseen surfaces keep residue (ticket 11; variant + line→sub-range table = §7 amendments A5/A6).
- **A/B capture mode** — always-compiled, cvar-gated (default off) engine mode running the sim on a synthetic clock — exactly one tick per rendered frame, interpolation fraction pinned at 0.5, sky scroll/`TexAnim`/FOV from tick time — writing the final composed framebuffer as per-tick PNGs; driven externally by `tools/abtest.py`; distinct from the existing `tools/abtest/` perf kit (ticket 09).
- **Acceptance matrix** — the set of A/B runs that must all pass: every acceptance map × {static tick lists, demo replay} × {GL33, Vulkan} (26 maps → 104 combinations, two fresh-level-load launches each), both backends must pass, plus ticket 07's cvar sweep (ticket 09).
- **Known-diff allowlist** — frozen, committed per-map JSON list of tolerated pixel-diff clusters (map, mode, tick, cluster rect/centroid + radius, reason); recorded at first bring-up, human-reviewed, limited to z-fighting; any new diff fails; entries can only be removed, never added silently (ticket 09; ADR 0004).
- **Soak** — human-driven ~half-day stability checklist with levelmesh on: full campaigns, portal/scale/secret/dither walks, 15-min deathmatch, light-thinker tour, stability watch incl. one 1 h+ session; one of the three passes required for PROVEN (ticket 09).
- **Proven** — declaration that the levelmesh path passed the A/B matrix (zero tolerance outside the allowlist), the perf bars, and the soak, with no open levelmesh-only bug of severity ≥ S2; implementer runs/posts, map owner declares, `gl_uselevelmesh` default flip + SettingsPage entry ship with it (ticket 09).

## 3. Geometry data model (ticket 04 — spec §3)

**Warp model: baked XYZ, dynamic-only eval.** (User override of the grilling's eval-all recommendation.) Static surfaces draw baked z/v directly; only dynamic-flagged surfaces evaluate z in the VS from the sector state buffer (§4): `z = dot(normal, xy) + D`; `V = z - plane.TexZ + vparam`. No per-vertex interpolation state; the state buffer carries the single current, CPU-interpolated plane values (ADR 0002).

**Storage: one whole-level vertex pool + per-region IBOs; pool ordering per region, then per piece.** No per-portal vertex copies. 32-bit indexing throughout (memory budget below). Main region block first, then each portal region's block; within a block, subsector fans and line quads are contiguous ranges. A region is a self-contained vertex+index block (independent allocation/streaming later); re-bake = one sub-upload per (region, sector) pair.

**Class: new backend-neutral class; `DoomLevelMesh` demoted to the raytrace adapter.** The model lives in `src/common/rendering/` (naming tentative; ticket 04 suggests `FLevelMesh`) so both GL33 and Vulkan draw paths consume it. `VkRaytrace` itself is unchanged: a small adapter in `hwrenderer/` feeds it the same load-time frozen geometry (positions + indices) today's builder produces, keeping the raytracer's rebuild-on-mesh-pointer-change contract (`vk_raytrace.cpp:49-60`). Note: `DoomLevelMesh::IsControlSector` is disabled (returns `false`, `doom_levelmesh.cpp:382-386`) — the adapter exposes the full surface set; no control-sector skip to preserve.

**Fake flats = extra static surfaces.** Each 3D floor/flood surface (F3DFloor model sector) gets its own floor + ceiling fan in the same pool, plane refs pointing at the model sector (`heightsec`). No second pass, no runtime copies. The 3D-floor *structure* is static data: line special 160 (`Sector_Set3DFloor`) is `LS_NOP` at runtime (`p_lnspec.cpp:3707`); `P_Recalculate3DFloors` (from `P_ChangeSector`) only recomputes heights/lights from the control planes — sector movement never invalidates the pool.

**Dynamic lifecycle: flag flip + re-bake to static at rest.** (User override of stays-dynamic.) First movement → dynamic flag on every surface referencing the sector's planes, and the vertices' dual-purpose slot rewritten `(z,v)` → `(vparam,0)` via sub-upload. Movement completes + sector rests at a NEW height → CPU re-bakes baked `(z,v)` from the settled final (non-interpolated) height and clears the flag. Re-bake-eligible only when ALL referenced planes have settled; settled-but-dynamic surfaces (partner still moving) simply wait. No "once dynamic, always dynamic". Scrolling/transdoor/alpha sectors are marked dynamic at map load. The snapshot pass (§4) drives flip and re-bake.

**Vertex format: 32-byte uniform layout with a dual-purpose 8-byte slot.** One attribute set serves static and dynamic surfaces; the surface record's dynamic flag selects the slot interpretation. Lightmap UVs stored on every vertex (static; ignored when the surface has no lightmap).

**Unpegged/middle/lower windows: computed in the VS.** Clamps derive from the four evaluated planes (front floor/ceiling, back floor/ceiling) + per-surface static offsets (opening mask, unpegged flag, window offsets). No per-frame vertex upload exists anywhere in the model; code path uniform for static and dynamic walls.

**Surface records in a CPU-writable SSBO, reached by a per-vertex uint32 index.** One record per wall quad / per subsector fan (today's `Surface` granularity). Runtime texture swaps and flag flips are single-record sub-uploads; the VS does one SSBO fetch per vertex. (Note: §7 implements the record buffers as uniform texel buffers / `samplerBuffer` on both backends — GL 3.3 core has no SSBOs.)

**Vertex — 32 bytes, stride 32:**

| off | type | field |
|---|---|---|
| 0 | float | x |
| 4 | float | y |
| 8 | float | slotA — static: baked z \| dynamic: vparam |
| 12 | float | slotB — static: baked v \| dynamic: 0 |
| 16 | float | u |
| 20 | float | lmu — lightmap UV (always stored) |
| 24 | float | lmv |
| 28 | uint32 | surfaceIndex |

`vparam`: `V = evaluatedZ - plane.TexZ + vparam` (Doom wall/flat scale is always 1; wall vparam folds in the unpegged/window offsets that do not track a plane).

**Surface record — 64 bytes (SSBO, CPU-writable), one per wall quad / subsector fan:**

| off | type | field |
|---|---|---|
| 0 | uint32 | textureIndex (04, MUTABLE: ACS texture swaps, single-record sub-upload) |
| 4 | uint32 | regionIndex (04) |
| 8..24 | uint32[4] | planeRef[4] — each: sectorIndex<<1 \| planeBit (0=floor, 1=ceiling); flat: [0]=own floor, [1]=own ceiling; wall: [0]=front.floor [1]=front.ceil [2]=back.floor [3]=back.ceil |
| 24 | uint32 | lightRef (04; offset normalized) — sectorIndex<<2 \| slot (0=floor, 1=ceiling, 2=wall); static, resolved through heightsec transfer-light relationships at build |
| 28 | uint32 | flags (04; offset normalized) — bits below |
| 32 | float[4] | vparam[4] ([3] reserved) — [0] lower unpegged offset (rel. front floor), [1] middle window offset, [2] upper window offset |
| 48 | int16 | light (07) side->Light |
| 50 | int16 | tierLight (07) side->TierLights[tier] |
| 52 | int16 | relSmooth (07) baked fake contrast, smooth formula (build-time CPU) |
| 54 | int16 | relNonSmooth (07) baked fake contrast, classic formula |
| 56 | int16 | lightlistRef (07) 3D-light index into the per-level light state buffer; -1 = none |
| 58 | uint16 | bandOffset (07) into the per-level static band table (walls in 3D volumes) |
| 60 | uint16 | bandCount (07) 1 = ordinary wall (band 0 = this record, no split planes) |
| 62 | uint8[2] | reserved |

**`flags` uint32 bits** (04 bits kept, 07 bits added):

| bits | meaning |
|---|---|
| 0 | dynamic (04, MUTABLE) |
| 1 | sky (04) |
| 2 | hasLightmap (04) |
| 4–6 | openingMask: upper / lower / middle (04) |
| 7 | unpeggedLower (04) |
| 8–9 | tier: 0 upper / 1 middle / 2 lower (07) |
| 10 | WALLF_ABSLIGHTING (07) |
| 11–13 | WALLF_ABSLIGHTING_TIER[3] (07) |
| 14 | WALLF_NOFAKECONTRAST (07) |
| 15 | WALLF_SMOOTHLIGHTING (side) (07) |
| 16 | WALLF_LIGHT_FOG (07) |
| 17 | 3D FF_FOG — flats only (07) |
| 18–25 | lightmapNum (07) |
| 26–27 | skyScrollKind: 0 sky1 / 1 sky2 / 2 mist (07) |
| 28–29 | skyScrollKind2 — doublesky second layer (07) |
| 30 | m2snf — fake infinite wall: fog runs at light 255 / rel 0 (07; `hw_walls.cpp:291`) |
| 31 | reserved |

Flats use only `lightlistRef` of the 07 wall block. **07 amendment:** ticket 04 sized this record 48 bytes (old offsets: `lightRef`@32 / `flags`@36 / `vparam[4]`@40 — an 8-byte hole, since `planeRef[4]` already spans 8..24); ticket 07 normalized those offsets and grew the record to 64 bytes by appending the 16-byte wall-shading/sky block above.

**Region record — container locked here, semantics owned by section 5 (ticket 06):** `vertexOffset`/`vertexCount` (contiguous block in the pool); `iboOffset`/`iboCount` (into the region's index buffer); `texRangeOffset`/`texRangeCount` (per-texture sub-range table: `{ textureIndex, iboOffset, iboCount }` per (region, texture)); `transform` (remapped viewpoint: rotation/translation/angle diff/z rebase); `frontFace` CCW \| CW — MUST be inverted for mirrored portal regions (negative-determinant transform flips winding); scissor/stencil state.

**Sector state buffer — requirements only** (layout + streaming owned by §4): indexed by `sector_t` index. Per sector: floor + ceiling plane (normal, D, `TexZ`), light raw ingredients (`lightlevel` + per-plane Light offsets + the 9-byte `FColormap` — layout is §4's). Plane slots consumed only by dynamic-flagged surfaces (plus dynamic walls for window math); light by all surfaces (texture indices live in the surface record, not the state buffer). §4 must also define the dirty-sector stream and a **settled** event (movement completion at the final non-interpolated height) — that event is this section's re-bake trigger, and the first-movement flag flip is ordered against the sector's first state-buffer upload (both on the render-thread update path).

**Memory budget.** Typical 50k-line level: ~1M vertices ≈ 32 MB pool, ~10 MB IBO, 64–128 MB surface records; slaughter-class (200k lines, several 3D-floor layers): ~3–5M vertices ≈ 100–160 MB pool, ~20 MB IBO, 190–320 MB records (figures use the 07-amended 64-byte record). 32-bit index ceiling = 4.29B vertices = 137 GB of 32-byte data — 32-bit indexing is safe by orders of magnitude; 64-bit indexing ruled out (not in GL33 core, no Vulkan core path — non-portable).

**Build order** (map load; replaces/augments the `DoomLevelMesh` build at `maploader.cpp:3251`):
1. Resolve plane references for all sectors — incl. control / 3D-floor model sectors — and transfer-light relationships into `lightRef`.
2. Enumerate surfaces: line quads (upper/lower/middle, both sides, incl. 3D-floor boundaries), subsector floor/ceiling fans (degenerate triangles skipped — parity with the existing builder), extra fake-flat + flood surfaces per 3D-floor model.
3. Assign regions (§5), then emit per region: contiguous per-piece vertices + per-texture-sorted indices.
4. Fill the surface record SSBO.
5. Initialize the sector state buffer (initial planes/lights — owned by §4).
6. VkRaytrace snapshot: build the load-time frozen geometry (positions + indices) for the adapter; raytracer behavior unchanged.

**Runtime mutation paths** (all over the write-once pool; timing owned by §4's render-thread update protocol):
- First movement: set `dynamic` on every referencing surface; rewrite dual-purpose slots over the affected per-(region, sector) vertex ranges.
- Per tick: stream the state buffer for dirty sectors (§4 — implemented as the full per-frame copy, ADR 0003).
- Settled (§4 event): re-bake — patch baked `(z,v)`, clear `dynamic` (only when all referenced planes have settled).
- Texture swap (ACS): single surface-record sub-upload.
- Rebuild: level load only (new `FLevelLocals`); sector movement never rebuilds (structure static — fake flats).

**Handoffs.** Owes → §4: per-sector (planes, light) indexed by sector index; texture indices live in the surface record; re-bake consumes the settled event; flag flip ordered against first upload. → §5: fills the region record (transform/frontFace/scissor-stencil) and assigns regions; mirrored portal regions set `frontFace = CW`. → §6: `lightRef` → state-buffer light slots; `sky` flag on surface records; sky surfaces geometrically ordinary (sky texturing is §6's). → §7: the per-region per-texture IBO sub-range table, the vertex format, surface records; a draw = one (region, texture, bandIndex) sub-range, (region, texture) is band 0; transparency is a texture property; path-selection cvar/SettingsPage entry is §7's. → §8: no data-model requirements beyond §4's. Consumes from: §5 (region partition + record semantics), §6 (band table, fake-contrast selection/application, wall-shading block), §4 (settled event, state-buffer layout).

## 4. Dynamic sector state (ticket 05 — spec §4)

**Streaming: full per-frame copy.** All sector records full-copied every frame into a persistent ring buffer; the VS selects the frame slot by uniform. No dirty tracking for streaming (flicker lights dirty many sectors per tick anyway; ~50k sectors × 96 B ≈ 4.5 MB/frame is negligible; byte-identical frames help the A/B tool). Deliberate deviation from Helion's dirty-only streaming (ADR 0003).

**Record contents: the full record.** Tilted planes, scroll, light (raw ingredients: `lightlevel` + per-plane Light offsets + the 9-byte `FColormap`), glow, transdoor (layout below; 05's `skyOffset` slot is reserved — see the record note). Runtime texture changes stay out: the mutable texture index lives in the §3 surface record.

**Change detection: snapshot-pass parameter diff.** The classic `vboheight` mechanism (`hw_vertexbuilder.cpp:443`) applied to records. No playsim hooks. Scroll sectors are marked dynamic **at load** from their sector special (Helion's `StaticDataApplier` pattern) and never hit the re-bake path. Load-marked dynamics: scroll (floor/ceiling special), transdoor, alpha.

**Helion parity rationale: single current value.** UZDoom's `DoInterpolations(I_GetTimeFrac())` (`d_main.cpp:585`; blended-plane write at `r_interpolate.cpp:470`) already writes the tick-blended plane into `sector_t` before the renderer runs, and the classic path bakes exactly those values per frame — so the levelmesh warps the same numbers the classic path renders: A/B pixel parity on moving geometry is exact by construction.

**Locked by code investigation (no decision needed):**
- **Frame flow:** no async render thread in the HW path (`r_thread.h` is software-renderer residue). `D_Display` → `D_Render(action, true)`: per level `DoInterpolations(I_GetTimeFrac())` → `RenderView` → `RestoreInterpolations()` (`d_main.cpp:573-593`). The snapshot pass runs where the classic `CheckUpdate` runs — inside frame setup, after interpolation.
- **Existing machinery the levelmesh replaces:** per-plane `vboheight` compare + sub-upload (`hw_vertexbuilder.cpp:443`), `vertex_t::dirty` → `RecalcVertexHeights` (`hw_walls.cpp:2189`), and fresh-per-frame reads of light/colormap/scroll — all three collapse into the snapshot pass.
- **3D floors, fake flats, `sector_link`: no special mechanism.** Every sector owns a record; a surface's plane refs point at its own or its `heightsec` **model sector**; the VS samples the *referenced* sector's record — that is how fake-flat surfaces and 3D-floor walls follow the model. Model sectors and `sector_link` targets (`p_linkedsectors.cpp`) are ordinary sectors moved by ordinary thinkers. `P_RecalculateAttached3DFloors` already runs inside interpolation, before the snapshot. Fake-flat model sectors must be same-level (classic path constraint).
- **Multi-level:** each `FLevelLocals` gets its own buffer + snapshot (the `AllLevels()` loop already does this for interpolation).

**Sector state record — 96 bytes, 4-byte aligned, SSBO:**

| off | type | field |
|---|---|---|
| 0 | float[4] | floorPlane (05) — tilted floor (nx,ny,nz,d), classic secplane_t |
| 16 | float[4] | ceilPlane (05) — tilted ceiling plane |
| 32 | float[2] | floorScroll (05) — floor UV offset (applied in VS) |
| 40 | float[2] | ceilScroll (05) |
| 48 | uint16 | lightlevel (07) — raw sector->lightlevel (0..255) |
| 50 | int16[2] | planeLight (07) — [0] floor, [1] ceiling: secplane Light offset |
| 54 | uint16 | flags — bit0 transdoor (05; VS applies the classic z-1 floor offset) \| bit1 floor ABSLIGHTING \| bit2 ceil ABSLIGHTING (07) |
| 56 | uint32 | glowFloorColor (05, 07) — packed PalEntry (ABGR); 0 = texture-glow fallback, ~0u = glow disabled |
| 60 | float | glowFloorHeight (05, 07) |
| 64 | uint32 | glowCeilColor (05, 07) — same sentinels: 0 = texture-glow fallback, ~0u = disabled |
| 68 | float | glowCeilHeight (05, 07) |
| 72 | uint8[9] | colormap (07) — FColormap: LightColor(3) BlendFactor(1) Desat(1) FadeColor(3) FogDensity(1); fire/pulse/damage thinkers write these per tick |
| 81 | uint8[3] | reserved (07) |
| 84 | float[2] | reserved — 05's skyOffset slot (07's round-3 sky split: sky scroll is the per-level levelSkyPos uniform, §6) |
| 92 | uint32 | reserved (07) |

- Tilted planes (not bare z) so the VS warp matches the classic per-vertex `ZatPoint` derivation exactly, incl. slanted flats; `GetPlaneTexZ` values during the frame are the interpolated ones — the record never carries prev/current.
- **Light slots carry raw ingredients, not derived effective light (amended by ticket 07).** The VS derives the classic per-plane effective light through the ported light chain (§6). A surface reads the record of the sector its plane refs point at (model sector for fake flats), matching transfer-light sector semantics. The glow slots keep 05's fields and sentinels, shifted for alignment by the re-slot.
- **07 amendment.** 05's `colormap` uint32 was annotated "selfmap" — wrong: the HW path never reads `selfmap` (a software-renderer field); 07 replaces it with the real 9-byte HW `FColormap`. 05's `skyOffset[2]` slot (at 76) is now **reserved** (sky scroll → per-level per-frame uniform, §6). Record size unchanged (96 bytes); the per-frame full-copy write stands as written. (Historical: 05's original slots were `light[3]` effective at 48, `colormap` uint32 at 56, `skyOffset[2]` at 76, `reserved[2]` at 84.)

**Update protocol.**
- **Load.** Build the CPU record array (`num_sectors × 96 B`) and the persistent ring (`HW_MAX_PIPELINE_BUFFERS` copies; Vulkan mapped memory / GL persistent-mapped — backend mechanics are §7's). Mark load-dynamic sectors: scroll specials (floor/ceiling), transdoor/alpha flats → their surfaces get the dynamic flag in the surface record. Fake-flat surfaces referencing a potentially-moving model get the dynamic flag at load too (their plane refs track the model sector).
- **Per frame** (levelmesh frame setup, after `DoInterpolations`, inside `D_Render`'s action):
  1. **Pack.** Iterate `Level->sectors`; read planes (`floorplane`/`ceilingplane`), scroll (`planes[].xform`), light raw ingredients (`lightlevel`, per-plane Light offsets, `Colormap`), glow, transdoor → write the current ring slot (full copy).
  2. **Diff.** Compare each record's plane+scroll words against the cached previous-frame copy. First change → sector `moved` this frame; two consecutive unchanged frames → sector `settled` (a moving plane changes every frame at any speed, so N=2 is sufficient).
  3. **Lifecycle** (per §3): a *static* surface whose plane refs include a `moved` sector → flip its dynamic flag in the surface record + rewrite its dual-purpose slot `(baked z, baked v)` → `(vparam, 0)`. A *dynamic* surface whose referenced sectors are all `settled` → **re-bake**: write the current planes' z back into its vertex-pool range, restore the slot, clear the flag.
- **Write rules (pipeline hazard).** The per-frame full copy touches only the *current* slot — that is the ring's purpose. Rare small writes (re-bake z patches, first-movement slot rewrites, surface-record flag or texture-index changes) are applied to **all** ring slots (≤ 4 small sub-uploads) so in-flight frames never read half-updated state. `RestoreInterpolations()` runs after frame setup; nothing further is coupled to it. Non-interpolated frames (cutscenes, `r_NoInterpolate`) pack raw tick values, exactly as the classic path renders them.
- **GPU side.** For a dynamic surface the VS: `s = state[slot][surface.planeRef.sector]`; `z = ZatPoint(s.plane, vertex.xy)` (plus the transdoor z-1 offset when flagged); `uv += s.scroll`; light/colormap/glow come from `s` (shading model: §6). Static surfaces never touch the buffer. The per-frame slot index is a single uniform.

**Helion-alignment deviations (3).** Streaming: Helion dirty-only (3 floats per changed sector) → full per-frame copy (bandwidth negligible; A/B determinism). Tick interpolation: prev+current in a dynamic VBO, VS mixes → single current value, CPU-interpolated (`DoInterpolations` already blends; exact classic parity). Dynamic storage: second per-plane dynamic VBO → none — VS warp from the state buffer (per §3). (Aligned: per-sector GPU state buffer sampled per vertex; light changes never touch vertices; load-time dynamic marking.)

**Handoffs.** Owes → §3: drives the lifecycle §3 locks — the `moved`/`settled` diff is the flag-flip and re-bake trigger (the "settled" event §3 requires); the write rules order the first-movement flag flip against the sector's first state-buffer upload; §3's "dirty-sector stream" requirement is met by the full per-frame copy instead (ADR 0003). → §6: consumes the record's light, colormap, glow, and transdoor fields in its per-vertex shading chain; its approved amendment re-slots the light slots (record note above). → §7: owns the ring's backend storage mechanics (Vulkan mapped memory / GL persistent-mapped) and identifies this ring as the render handoff ring — the per-frame sector-state slot is part of its frame record, and its render worker consumes the frozen slot contents; the snapshot pass runs where §7 places the frame build (game thread, inside the interpolation window), and 08's later async render worker does not move it (the "no async render thread" observation describes the existing HW path).

## 5. Codebase current state (verified in this task)

**EXISTS TODAY (sections 3–4 territory):**
- Backend-neutral base class `hwrenderer::LevelMesh` — `src/common/rendering/hwrenderer/data/hw_levelmesh.h:25`. Data-only: `TArray<FVector3> MeshVertices`, `TArray<int> MeshUVIndex`, `TArray<uint32_t> MeshElements`, `TArray<int> MeshSurfaces`. No UVs, lighting, texture bindings, per-region structure, or dual-purpose slot.
- `src/rendering/hwrenderer/doom_levelmesh.{h,cpp}` (`DoomLevelMesh : hwrenderer::LevelMesh`; 402-line .cpp). Builds today: line quads (lower/upper/middle, both sides, incl. 3D-floor boundary quads with `controlSector = xfloor->model`; sky detection via `bSky`), subsector floor/ceiling fans (incl. fake-flat floor+ceiling fans per F3DFloor model), degenerate triangles skipped; fills `MeshVertices`/`MeshElements`/`MeshSurfaces` plus per-surface `Surface` records `{type, typeIndex, numVerts, startVertIndex, secplane_t plane, controlSector, bSky}`. Bakes from current planes via `ZatPoint` at load. `IsControlSector` is disabled (returns `false`, check commented out, `doom_levelmesh.cpp:382-386`) — control-sector surfaces are emitted today, matching the spec note.
- Rebuild contract: `VkRaytrace::SetLevelMesh` (`src/common/rendering/vulkan/renderer/vk_raytrace.cpp:49-60`) — on mesh-pointer change: `Reset()` + `CreateVulkanObjects()` (if raytracing enabled); `NullMesh` fallback cube.
- Build call site: `maploader.cpp:3251` `Level->levelMesh = new DoomLevelMesh(*Level);` (after `FinalizePortals` + AABB tree); deleted at teardown (`p_setup.cpp:378-380`). Only consumer chain: `hw_entrypoint.cpp:108` `screen->SetLevelMesh(camera->Level->levelMesh)` → `v_video.h:178` (no-op base virtual) → `vk_renderdevice.cpp:524` → `VkRaytrace::SetLevelMesh` — the GL renderer does not consume the mesh ("only consumer = Vulkan raytracer" verified).
- Frame flow: `D_Render` (`d_main.cpp:575-595`) — per-level `DoInterpolations(I_GetTimeFrac())` @585 → `action()` @589 (the `D_Display` lambda calls `RenderView`, `d_main.cpp:1329-1331`) → `RestoreInterpolations()` @593. Blended-plane write site: `r_interpolate.cpp:470` = `DSectorPlaneInterpolation::Interpolate` — writes the tick-blended `setD`/`SetPlaneTexZ` into `sector_t`, calls `P_RecalculateAttached3DFloors(sector)` (`r_interpolate.cpp:496`) + `CheckPortalPlane` (the `DoInterpolations` entry itself is `r_interpolate.cpp:236`).
- `HW_MAX_PIPELINE_BUFFERS` (`src/common/rendering/hwrenderer/data/buffers.h`): 4 @32 (Android), 2 @36 (desktop) — the spec's `buffers.h:32/36` cite is exact.
- Machinery the snapshot pass replaces: per-plane `vboheight` compare + `UpdatePlaneVertices` sub-upload in `CheckPlanes` (`hw_vertexbuilder.cpp:443`); `vertex_t::dirty` → `RecalcVertexHeights` (`hw_walls.cpp:2189`, gl_seamless path). `src/common/rendering/r_thread.{h,cpp}` = software-renderer `DrawerThread` span drawer (`r_multithreaded`) — never used by the HW path (confirms the "no async render thread" fact).
- Where a backend-neutral class would live: `src/common/rendering/` subdirs `gl/`, `gl_load/`, `hwrenderer/` (with `data/`, `postprocessing/`), `vulkan/`; the existing `LevelMesh` base sits in `hwrenderer/data/` — the seam already exists. Multi-level: `AllLevels()` (`g_levellocals.h:1004`, inline `TArrayView<FLevelLocals*>`) — `D_Render`'s interpolation/restore loops already iterate per level (the snapshot-pass loop pattern already exists).
- Line special 160 `Sector_Set3DFloor` = `LS_NOP` at runtime (`p_lnspec.cpp:3707`) — 3D-floor structure is static, as the spec assumes.

**DOES NOT EXIST YET:**
- The `FLevelMesh` (or renamed) class + backend-neutral builder, and the levelmesh draw path (frame build, 3D pass, render worker) — nothing in the tree; only the data-only `LevelMesh` base exists.
- `gl_uselevelmesh` cvar — zero case-insensitive hits for `uselevelmesh` across `src/`. Grep hits for `levelmesh` that DO exist: the `LevelMesh` base + `DoomLevelMesh` + `SetLevelMesh` chain above, the `maploader.cpp:3251` construction, and a perflog comment (`hw_clock.cpp:219-221`, `r_perflog` "Used by the levelmesh comparison runs").
- The sector state buffer (96-byte `SectorState` records) and the snapshot pass.
- The handoff ring as frame record — the ring constant exists (sizes the current per-frame `mBufferPipeline` buffers) but no ring holds a frame record.

## 6. Slices-relevant notes (future implementation slice covering spec §3–§4)

- Must produce: the `FLevelMesh` builder (whole-level pool + per-region IBOs + 64 B surface-record buffer + region records) at the `maploader.cpp:3251` site; the 96 B sector-state ring + snapshot pass (Pack/Diff/Lifecycle) at the `D_Render` action site; the 32 B dual-purpose vertex format; re-bake + first-movement slot-rewrite sub-uploads with the all-ring-slot write rule; the `gl_uselevelmesh` Bool cvar (`CVAR_ARCHIVE|CVAR_GLOBALCONFIG`, default false) + path selection at level setup.
- Must-ship deliverable: the `VkRaytrace` adapter — keep the existing rebuild-on-mesh-pointer-change contract (`vk_raytrace.cpp:49-60`) fed with load-time frozen geometry (standing constraint). The raytracer consumes only positions + indices, so the adapter is a thin projection of the new pool.
- Reuse: the existing `DoomLevelMesh` builder already enumerates exactly the surfaces §3 step 2 requires (both-sides line quads incl. 3D-floor boundaries, subsector fans incl. fake-flat floor+ceiling fans, degenerate-triangle skip, sky detection) and bakes from current planes via `ZatPoint` — a ready enumeration core consistent with the baked-XYZ model.
- Replace (on the levelmesh path only; the classic path keeps them): per-plane `vboheight` sub-upload (`hw_vertexbuilder.cpp:443`), `vertex_t::dirty`/`RecalcVertexHeights` (`hw_walls.cpp:2189`), fresh-per-frame light/colormap/scroll reads — all three collapse into the snapshot pass.
- Seam: the new class sits beside/extends the existing `hwrenderer::LevelMesh` base (`src/common/rendering/hwrenderer/data/hw_levelmesh.h`) — keep (or deprecate) its arrays so the adapter + `VkRaytrace` keep working unchanged.
- `IsControlSector` already returns false → the pool contains control-sector geometry today; the classic path expresses control sectors through the fake flats the builder already emits — no skip logic to add in the adapter.
- Hard parts: re-bake/first-movement writes must go to ALL ring slots (pipeline hazard) while the full copy touches the current slot only; the snapshot pass must run inside the interpolation window — the current `RenderView` lambda (`d_main.cpp:1329-1331`) is the only slot; scroll/transdoor/alpha load-marking must be derived from sector specials (no playsim hooks). Easy parts: the HW frame flow is synchronous on the game thread (no existing render thread to coordinate with); the multi-level `AllLevels()` loop and the ring constant already exist; records index trivially by `sector_t` index.

## Discrepancies

- None blocking. One cite-precision note: the spec cites `DoInterpolations(I_GetTimeFrac()) (d_main.cpp:585, r_interpolate.cpp:470)` — `r_interpolate.cpp:470` is not the `DoInterpolations` entry (that is line 236); it is `DSectorPlaneInterpolation::Interpolate`, the per-sector write site of the tick-blended plane. The spec's claim (blended planes written into `sector_t` before the renderer runs) is accurate; the cite points at the write site, not the entry. All other cites verified exact against the current tree: `vk_raytrace.cpp:49-60`, `maploader.cpp:3251`, `d_main.cpp:573-593`/`585`, `buffers.h:32/36`, `hw_vertexbuilder.cpp:443`, `hw_walls.cpp:2189`, `doom_levelmesh.cpp:382-386`, `p_lnspec.cpp:3707`, `hw_walls.cpp:291`, the only-consumer chain, and the "no async render thread" fact.
