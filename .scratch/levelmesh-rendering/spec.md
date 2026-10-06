# Levelmesh Rendering — Handoff Spec

This spec is the destination of the wayfinder map at `.scratch/levelmesh-rendering/map.md`. It
consolidates resolved tickets 04–11 plus ADRs 0001–0004 plus the measured classic-path baseline
(ticket 10) into one lock on every architectural decision for replacing UZDoom's per-frame
BSP-traversal rasterizer with a static levelmesh pipeline. Implementation is a separate, later
effort that picks this spec up.

## 1. Destination, scope, and standing constraints

**What the levelmesh pipeline is.** The levelmesh path replaces UZDoom's per-frame BSP-traversal
rasterizer (front-to-back traversal in `hw_bsp.cpp` → drawsegs → multithreaded job pool → batches)
with a Helion-style static levelmesh pipeline (map.md, Destination): a whole-level static mesh
built at map load, drawn z-buffered, with per-sector dynamic state evaluated in the shaders.
Dynamic sectors (moving/animated planes and their light) are handled by a per-sector state buffer
+ vertex-shader warp over the static mesh — Doom topology is static, so no runtime
re-triangulation (map.md, charting round; see ADR 0002), and the state buffer is streamed in full
every frame with no dirty tracking (see ADR 0003). The spec is Vulkan-first, but the GL 3.3 draw
path is designed in the same spec — both backend draw paths are specified (map.md, charting
round). Feature scope is full parity with the classic path, including `PORTAL` line specials and
`sector_link`; portal rendering is a first-class design problem, not a compatibility shim (see
ADR 0001). The classic BSP rasterizer is retained as a selectable fallback, and levelmesh becomes
the default only once proven (map.md, charting round). The acceptance bar includes a
deterministic A/B rendering tool — both paths, same map + camera, zero-tolerance pixel diffing
against a frozen known-diff allowlist (map.md, charting round; see ADR 0004).

**Domain.** UZDoom hardware rendering — `src/rendering/hwrenderer/`,
`src/common/rendering/{vulkan,gl,hwrenderer}/`. Key existing state: `DoomLevelMesh`
(`src/rendering/hwrenderer/doom_levelmesh.*`) already builds a whole-level static mesh at map
load; its only consumer today is the Vulkan raytracer (`VkRaytrace`) (map.md, Notes).

**Out of scope** (map.md, verbatim-faithful):

- Gameplay sight/LOS logic (`P_CheckSight`, visual thinkers) — unchanged; ticket 11 proved renderer coupling and its dither tail *calls* `P_CheckSight`/`SetDitherTransFlags` verbatim (reused, not modified).
- Vulkan raytracer feature work beyond "keep working".
- Upstreaming to ZDoom/GZDoom (CONTRIBUTING.md prohibits AI-generated contributions upstream).
- Software renderer and GLES backend — already removed (context, not scope).

**Standing constraints.**

- The Vulkan raytracer must keep working: `DoomLevelMesh`'s only consumer today is the Vulkan
  raytracer, and the levelmesh geometry work must not break it (map.md, Notes; the raytracer is
  fed by a small adapter per ticket 04).
- The classic BSP path stays selectable as a fallback; levelmesh becomes the default once proven.
  `gl_uselevelmesh` is a Bool cvar (`CVAR_ARCHIVE | CVAR_GLOBALCONFIG`) with default **false** at
  handoff, and the default flip ships with the PROVEN declaration (map.md, charting round;
  tickets 08/09).
- Vulkan-first, but both backend draw paths (Vulkan and GL 3.3) are specified in this spec
  (map.md, charting round).
- The mesh/sector-state data layer is backend-neutral (map.md, charting round).
- Sprites stay on the existing sprite batch path, drawn in the same pass z-buffered against the
  levelmesh (map.md, charting round; ticket 08).
- Backends are GL 3.3 + Vulkan only — the software renderer and GLES backend were already
  removed (Oct 2026); context, not scope (map.md, Notes).
- `CONTRIBUTING.md` prohibits AI-generated code contributions to upstream ZDoom/GZDoom — this is
  fork work; flag this before preparing any commit/PR (map.md, Notes).

## 2. Glossary

The terms below are as defined by the resolved tickets (04–11); each definition cross-references
the section that specifies the term.

**Render worker (levelmesh).** The Vulkan-only render worker thread added by the levelmesh path:
it owns the 3D world pass only (per-frame handoff-slot uploads, the full 3D issue, occlusion-query
issue, per-slot completion fence) and consumes frozen slot contents — never playsim state. The game
thread keeps sim, the frame build (inside the interpolation window), the 2D stack, and presentation;
the GL33 path has no worker (its 3D pass issues synchronously on the game thread). Specified in
section 7 (ticket 08).

**Handoff slot.** One slot of the handoff ring — 05's sector-state ring, `HW_MAX_PIPELINE_BUFFERS`
slots (2 desktop / 4 Android) — holding the per-frame frame record: sector-state records,
3D-light state, levelSkyPos, per-viewpoint values, portal-view list, culled draw list, sprite/model
vertex slot references, and the previous frame's query results. Processed in order, fence-gated
back-pressure, no frame skipping. Specified in section 7 (ticket 08).

**Window polygon.** The per-portal polygon that bounds a portal view: in the levelmesh a small
dynamic buffer of one quad per portal (static endpoints + the current heights of the two
portal-adjacent sectors), whose stencil/clip masks the portal's sub-range draws and its interior
depth clear, and which is the depth-only draw target of the per-portal occlusion queries that gate
the next frame's portal draws. Specified in section 5 (ticket 06, protocol) and section 7
(ticket 08, buffer plumbing and queries).

**Exposure pass.** The per-frame per-view candidate LOS pass that reproduces the classic's automap
fog: candidates (the view's culled draw list, per view mode) are ray-tested from the camera to
static fan-vertex test points by the BSP raycast over the glnodes tree, and any unoccluded
candidate marks `SSECMF_DRAWN` / `ML_MAPPED` (marks are monotonic). The per-frame `cullcolor`
latch from the culled list is part of the same pass. Specified in section 8 (ticket 11).

**Dither fragment.** A culled-draw-list append that re-draws an existing sub-range's index range
with the `DITHERTRANS` pipeline variant (`EFF_DITHERTRANS` / `main.fp #define DITHERTRANS`),
appended during the cull walk when the classic dither flags (`WALLF_DITHERTRANS_*`,
`dithertranscount` decremented by the line's per-sub-range seg count) are set — clear-on-consume,
unseen surfaces keep residue. Specified in section 8 (ticket 11; the pipeline variant and the
line→sub-range table are section 7's amendments A5/A6).

**A/B capture mode.** The always-compiled, cvar-gated (default off) engine mode that runs the sim
on a synthetic clock — exactly one tick per rendered frame, interpolation fraction pinned at 0.5,
sky scroll / `TexAnim` / FOV computed from tick time — and writes the final composed framebuffer as
per-tick PNGs for a tick list; driven externally by `tools/abtest.py`. Built by the implementation
effort, distinct from the existing `tools/abtest/` perf kit. Specified in section 9 (ticket 09).

**Acceptance matrix.** The set of A/B runs that must all pass: every acceptance map × {static tick
lists, demo replay} × {GL33, Vulkan} (26 maps → 104 combinations, two fresh-level-load launches
each), both backends must pass, plus ticket 07's cvar sweep. Specified in section 9 (ticket 09).

**Known-diff allowlist.** The frozen, committed per-map JSON list of tolerated pixel-diff clusters
(map, mode, tick, cluster rect/centroid + radius, reason), recorded at first bring-up, human-
reviewed, and limited to z-fighting; any new diff fails, and entries can only be removed, never
added silently. Specified in section 9 (ticket 09; ADR 0004).

**Soak.** The human-driven ~half-day stability checklist run with levelmesh on — full campaigns,
portal/scale/secret/dither walks, a 15-min deathmatch, a light-thinker tour, and a stability watch
including one 1 h+ session. One of the three passes required for PROVEN (with the A/B and perf
passes). Specified in section 9 (ticket 09).

**Proven.** The declaration that the levelmesh path has passed the A/B matrix (zero tolerance
outside the known-diff allowlist), the perf bars, and the soak, with no open levelmesh-only bug of
severity ≥ S2; the implementer runs and posts, the map owner declares, and the `gl_uselevelmesh`
default flip + SettingsPage entry ship with it. Specified in section 9 (ticket 09).

## 3. Geometry data model (ticket 04)

**What this section locks.** Ticket 04 (grilling, 3 rounds; resolved 2026-10-03, every branch
explicitly chosen) locks the static levelmesh's data model: the vertex format, the surface and
region record layouts, the whole-level storage scheme, the dynamic-surface lifecycle, and the
build order. Two branches were user overrides of the grilling's recommendation — the warp model
(baked XYZ, dynamic-only eval, over eval-all) and the at-rest policy (re-bake to static, over
stays-dynamic). The data layer is backend-neutral by mandate: it serves both the Vulkan and
GL 3.3 draw paths (section 7), and the raytracer remains a co-consumer (standing constraint,
section 1).

### Locked decisions

**Warp model: baked XYZ, dynamic-only eval.** Static surfaces draw baked z/v directly; only
surfaces carrying the dynamic flag evaluate `z` in the vertex shader from the sector state buffer
(section 4, ticket 05): `z = dot(normal, xy) + D`, `V = z - plane.TexZ + vparam`. No per-vertex
interpolation state; the state buffer carries the single current, CPU-interpolated plane values
(ADR 0002).

**Storage: one whole-level vertex pool + per-region index buffers.** No per-portal vertex copies.
32-bit indexing throughout (see memory budget). The pool is ordered per region, then per piece.

**Class: new backend-neutral class; `DoomLevelMesh` demoted to the raytrace adapter.** The model
lives in `src/common/rendering/` (naming tentative — this spec finalizes it; ticket 04 suggests
`FLevelMesh`) so both the GL33 and Vulkan draw paths consume it. `VkRaytrace` itself is unchanged:
a small adapter in `hwrenderer/` feeds it the same load-time frozen geometry (positions + indices)
that today's builder produces, keeping the raytracer's "rebuild on mesh-pointer change" contract
(`vk_raytrace.cpp:49-60`). Note: `DoomLevelMesh::IsControlSector` is disabled (returns `false`,
`doom_levelmesh.cpp:382-386`), so the adapter exposes the full surface set — no control-sector
skip to preserve.

**Fake flats = extra static surfaces.** Each 3D floor / flood surface (F3DFloor model sector) gets
its own floor + ceiling fan in the same pool, plane refs pointing at the model sector
(`heightsec`). No second pass, no runtime copies. The 3D-floor *structure* is static data: line
special 160 (`Sector_Set3DFloor`) is `LS_NOP` at runtime (`p_lnspec.cpp:3707`);
`P_Recalculate3DFloors` (called from `P_ChangeSector`) only recomputes heights/lights from the
control planes — sector movement never invalidates the pool.

**Dynamic lifecycle: flag flip + re-bake to static at rest.** A sector's first movement flips the
dynamic flag on every surface referencing its planes and rewrites the vertices' dual-purpose slot
(`(z,v)` → `(vparam,0)`) via sub-upload. When the movement completes and the sector rests at a NEW
height, the CPU re-bakes: it patches the baked `(z,v)` from the settled final (non-interpolated)
height and clears the flag. A surface is re-bake-eligible only when ALL planes it references have
settled; settled-but-dynamic surfaces (e.g. a partner still moving) simply wait. There is no
"once dynamic, always dynamic" state. Scrolling, transdoor, and alpha sectors are marked dynamic
at map load. The snapshot pass (section 4) drives the flip and the re-bake.

**Vertex format: 32-byte uniform layout with a dual-purpose 8-byte slot.** One attribute set serves
both static and dynamic surfaces; the surface record's dynamic flag selects the slot
interpretation (see buffer layouts). Lightmap UVs are stored on every vertex (static; ignored when
the surface has no lightmap).

**Unpegged / middle / lower windows: computed in the vertex shader.** The window clamps derive from
the four evaluated planes (front floor/ceiling, back floor/ceiling) + per-surface static offsets
(opening mask, unpegged flag, window offsets). No per-frame vertex upload exists anywhere in the
model; the code path is uniform for static and dynamic walls.

**Surface records in a CPU-writable SSBO, reached by a per-vertex uint32 index.** One record per
wall quad / per subsector fan (today's `Surface` granularity). Runtime texture swaps and flag
flips are single-record sub-uploads; the VS does one SSBO fetch per vertex.

**Pool ordering: per region, then per piece.** Main region block first, then each portal region's
block; within a block, subsector fans and line quads are contiguous ranges. A region is a
self-contained vertex+index block (independent allocation/streaming later); re-bake = one
sub-upload per (region, sector) pair.

### Buffer layouts

**Vertex — 32 bytes, stride 32:**

```text
offset	type	field
0	float	x
4	float	y
8	float	slotA	// static: baked z  |  dynamic: vparam
12	float	slotB	// static: baked v  |  dynamic: 0
16	float	u
20	float	lmu	// lightmap UV (always stored)
24	float	lmv
28	uint32	surfaceIndex
```

`vparam` semantics: `V = evaluatedZ - plane.TexZ + vparam` (Doom wall/flat scale is always 1; wall
`vparam` folds in the unpegged/window offsets that do not track a plane).

**Surface record — 64 bytes, one per wall quad / subsector fan (SSBO, CPU-writable):**

```text
offset	type	field
0	uint32	textureIndex	// (04, MUTABLE: ACS texture swaps, single-record sub-upload)
4	uint32	regionIndex	// (04)
8	uint32	planeRef[4]	// (04; spans 8..24) — each: sectorIndex<<1 | planeBit (0=floor, 1=ceiling)
			//   flat: [0]=own floor, [1]=own ceiling
			//   wall: [0]=front.floor [1]=front.ceil [2]=back.floor [3]=back.ceil
24	uint32	lightRef		// (04; offset normalized) — sectorIndex<<2 | slot (0=floor, 1=ceiling,
			// 2=wall); static, resolved through heightsec transfer-light relationships at build
28	uint32	flags		// (04; offset normalized) — bits below
32	float	vparam[4]	// (04; [3] reserved) — [0] lower unpegged offset (rel. front floor),
			// [1] middle window offset, [2] upper window offset
48	int16	light		// (07) side->Light
50	int16	tierLight	// (07) side->TierLights[tier]
52	int16	relSmooth	// (07) baked fake contrast, smooth formula (build-time CPU)
54	int16	relNonSmooth	// (07) baked fake contrast, classic formula (build-time CPU)
56	int16	lightlistRef	// (07) 3D-light index into the per-level light state buffer; -1 = none
58	uint16	bandOffset	// (07) into the per-level static band table (walls in 3D volumes)
60	uint16	bandCount	// (07) 1 = ordinary wall (band 0 = this record, no split planes)
62	uint8	reserved[2]
```

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
| 30 | m2snf — fake infinite wall: fog runs at light 255 / rel 0 (07; hw_walls.cpp:291) |
| 31 | reserved |

Flats use only `lightlistRef` of the 07 wall block; the rest of it is reserved for them. The
fake-contrast values are baked CPU-side at build (both formulas, no GPU atan) — selection and
application rules are section 6's.

*Amended by ticket 07 (section 6).* Ticket 04 sized this record 48 bytes with the offsets as then
documented (the old table listed `lightRef` at 32 / `flags` at 36 / `vparam[4]` at 40 — an 8-byte
hole: `planeRef[4]` already spans 8..24); ticket 07's approved amendment normalized those offsets
and grew the record to 64 bytes by appending the 16-byte wall-shading/sky block above.

**Region record — container locked here, semantics owned by section 5 (ticket 06):**

```text
uint32	vertexOffset, vertexCount	// contiguous block in the pool
uint32	iboOffset, iboCount	// into the region's index buffer
uint32	texRangeOffset, texRangeCount	// into the per-texture sub-range table:
				//   { textureIndex, iboOffset, iboCount } per (region, texture)
transform	// section 5: remapped viewpoint (rotation/translation/angle diff/z rebase)
frontFace	// CCW | CW — MUST be inverted for mirrored portal regions (section 5)
		// (negative-determinant transform flips winding)
scissor/stencil state	// section 5
```

**Sector state buffer — requirements only; layout and streaming owned by section 4 (ticket 05).**
Indexed by `sector_t` index. Per sector: floor + ceiling plane (normal, D, `TexZ`), light (raw
ingredients — `lightlevel` + per-plane Light offsets + the 9-byte `FColormap`; the layout is
section 4's). Plane slots are consumed only by dynamic-flagged surfaces (plus dynamic wall
surfaces for window math); light by all surfaces (texture indices live in the surface record, not
the state buffer). Section 4 must also define the dirty-sector stream and a **"settled" event** (movement
completion at the final non-interpolated height) — that event is this section's re-bake trigger,
and the first-movement flag flip must be ordered against the sector's first state-buffer upload
(both on the render-thread update path).

### Memory budget

Order-of-magnitude; sections 9 and 10 validate at true scale.

- Typical 50k-line level: ~1M vertices ≈ 32 MB pool, ~10 MB IBO, ~64-128 MB surface records.
- Slaughter-class (200k lines, several 3D-floor layers): ~3-5M vertices ≈ 100-160 MB pool, ~20 MB
  IBO, ~190-320 MB surface records. The 32-bit index ceiling is 4.29B vertices = 137 GB of 32-byte
  data — 32-bit indexing is safe by orders of magnitude. 64-bit indexing is not available in GL33
  core and has no Vulkan core path, so it is ruled out as non-portable.

The surface-record figures use the ticket-07-amended 64-byte record (surface-record count × 64 B);
ticket 07's growth supersedes the ticket-04-era (48-byte) figures.

### Build order

Map load; replaces/augments the `DoomLevelMesh` build at `maploader.cpp:3251`:

1. Resolve plane references for all sectors — including control / 3D-floor model sectors — and
   transfer-light relationships into `lightRef`.
2. Enumerate surfaces: line quads (upper/lower/middle, both sides, incl. 3D-floor boundaries),
   subsector floor/ceiling fans (degenerate triangles skipped — parity with the existing builder),
   extra fake-flat + flood surfaces per 3D-floor model.
3. Assign regions (section 5), then emit per region: contiguous per-piece vertices +
   per-texture-sorted indices.
4. Fill the surface record SSBO.
5. Initialize the sector state buffer (initial planes/lights — owned by section 4).
6. VkRaytrace snapshot: build the load-time frozen geometry (positions + indices) for the adapter;
   raytracer behavior unchanged.

**Runtime mutation paths** (all defined over the write-once pool; timing owned by section 4's
render-thread update protocol):

- First movement of a sector: set `dynamic` on every referencing surface; rewrite dual-purpose
  slots over the affected per-(region, sector) vertex ranges.
- Per tick: stream the state buffer for dirty sectors (section 4).
- Settled (section 4 event): re-bake — patch baked `(z,v)`, clear `dynamic` (only when all
  referenced planes have settled).
- Texture swap (ACS): single surface-record sub-upload.
- Rebuild: level load (new `FLevelLocals`). Sector movement never rebuilds (structure static, see
  fake flats).

### Handoffs to dependent sections

- **Section 4 (ticket 05, sector state):** owns state-buffer layout/streaming, dirty-sector set,
  "settled" event, and the research-02 graduates (per-frame exposure pass, dither hook). Contract
  from this section: per-sector (planes, light) indexed by sector index; texture indices live in
  the surface record (this section); re-bake consumes the settled event; flag flip ordered against
  first upload.
- **Section 5 (ticket 06, region model):** fills the region record (transform, `frontFace`,
  scissor/stencil) and assigns regions; mirrored portal regions must set `frontFace = CW`.
- **Section 6 (ticket 07, lighting/sky):** consumes `lightRef` → state-buffer light slots; `sky`
  flag on surface records. Sky surfaces are geometrically ordinary (z from plane); sky texturing is
  section 6's.
- **Section 7 (ticket 08, draw organization):** consumes the per-region per-texture IBO sub-range
  table, the vertex format, and surface records; a draw = one (region, texture, bandIndex)
  sub-range — (region, texture) is band 0 (section 6); transparency is a texture property;
  path-selection cvar/ZWidget entry is section 7's.
- **Section 8 (ticket 11, game-logic feedback):** no data-model requirements beyond section 4's.

## 4. Dynamic sector state (ticket 05)

**What this section locks.** Ticket 05 (grilling, 4 decisions confirmed with the user; resolved
2026-10-03) locks the per-sector state mechanism: a **sector state buffer** of one 96-byte record
per sector, **full-copied every frame** into a ring of `HW_MAX_PIPELINE_BUFFERS` slots (2 desktop /
4 Android, `buffers.h:32/36`), carrying **single current CPU-interpolated** values; the **snapshot
pass** (per-frame, after `DoInterpolations`) both streams the buffer and drives section 3's
static↔dynamic lifecycle by per-frame parameter diffing. Helion's lifecycle and per-sector GPU
buffer concept are kept; Helion's dirty-only streaming, prev/current GPU mix, and dynamic VBO are
deliberately **not** adopted (ADR 0002, ADR 0003).

### Locked decisions

**Streaming: full per-frame copy.** All sector records are full-copied every frame into a persistent
ring buffer; the VS selects the frame slot by uniform. No dirty tracking for streaming (flicker
lights dirty many sectors per tick anyway; ~50k sectors × 96B ≈ 4.5 MB/frame is negligible;
byte-identical frames help the A/B tool).

**Record contents: the full record** — tilted planes, scroll, light (raw ingredients: `lightlevel`
+ per-plane Light offsets + the 9-byte `FColormap`), glow, transdoor (layout below; 05's
`skyOffset` slot is reserved — see the record note). Runtime texture changes stay out: the mutable
texture index lives in the section 3 surface record.

**Change detection: snapshot-pass parameter diff** (classic `vboheight` mechanism,
`hw_vertexbuilder.cpp:443`, applied to records). No playsim hooks. Scroll sectors are marked
dynamic **at load** from their sector special (Helion's `StaticDataApplier` pattern) and never hit
the re-bake path.

**Helion parity: single current value.** UZDoom's `DoInterpolations(I_GetTimeFrac())`
(`d_main.cpp:585`, `r_interpolate.cpp:470`) already writes the tick-blended plane into `sector_t`
before the renderer runs, and the classic path bakes exactly those values per frame — so the
levelmesh warps the same numbers the classic path renders. A/B pixel parity on moving geometry is
exact by construction.

### Locked by code investigation (no decision needed)

- **Frame flow:** no async render thread in the HW path (`r_thread.h` is software-renderer residue).
  `D_Display` → `D_Render(action, true)`: per level `DoInterpolations(I_GetTimeFrac())` →
  `RenderView` → `RestoreInterpolations()` (`d_main.cpp:573-593`). The snapshot pass runs where the
  classic `CheckUpdate` runs — inside frame setup, after interpolation.
- **Existing machinery the levelmesh replaces:** per-plane `vboheight` compare + sub-upload
  (`hw_vertexbuilder.cpp:443`), `vertex_t::dirty` → `RecalcVertexHeights` (`hw_walls.cpp:2189`), and
  fresh-per-frame reads of light/colormap/scroll. All three collapse into the snapshot pass.
- **3D floors, fake flats, sector_link: no special mechanism.** Every sector owns a record; a
  surface's plane refs (section 3) point at its own or its `heightsec` **model sector**; the VS
  samples the *referenced* sector's record — that is how fake-flat surfaces and 3D-floor walls
  follow the model. Model sectors and sector_link targets (`p_linkedsectors.cpp`) are ordinary
  sectors moved by ordinary thinkers. `P_RecalculateAttached3DFloors` already runs inside
  interpolation, before the snapshot. Fake-flat model sectors must be same-level (classic path
  constraint).
- **Multi-level:** each `FLevelLocals` gets its own buffer + snapshot (the `AllLevels()` loop
  already does this for interpolation).

### Sector state record (96 bytes, 4-byte aligned, SSBO)

```c
struct SectorState {                 // 96 bytes
	float	floorPlane[4];    //  0 (05): tilted floor plane (nx,ny,nz,d) — classic secplane_t
	float	ceilPlane[4];     // 16 (05): tilted ceiling plane
	float	floorScroll[2];   // 32 (05): floor UV offset (applied in VS)
	float	ceilScroll[2];    // 40 (05): ceiling UV offset
	uint16	lightlevel;       // 48 (07): raw sector->lightlevel (0..255)
	int16	planeLight[2];    // 50 (07): [0] floor, [1] ceiling — secplane Light offset
	uint16	flags;            // 54: bit0 transdoor (05; VS applies the classic z-1 floor offset)
	                             //     | bit1 floor ABSLIGHTING | bit2 ceil ABSLIGHTING (07)
	uint32	glowFloorColor;   // 56 (05, 07): packed PalEntry (ABGR);
	                             //     0 = texture-glow fallback, ~0u = glow disabled
	float	glowFloorHeight;  // 60 (05, 07)
	uint32	glowCeilColor;    // 64 (05, 07): packed PalEntry (ABGR);
	                             //     0 = texture-glow fallback, ~0u = glow disabled
	float	glowCeilHeight;   // 68 (05, 07)
	uint8	colormap[9];      // 72 (07): FColormap — LightColor(3) BlendFactor(1) Desat(1)
	                             //     FadeColor(3) FogDensity(1); fire/pulse/damage thinkers
	                             //     write these per tick
	uint8	reserved[3];      // 81 (07)
	float	reserved[2];      // 84: 05's skyOffset slot — reserved (07's round-3 sky split;
	                             //     sky scroll is the per-level levelSkyPos uniform, section 6)
	uint32	reserved;         // 92 (07)
};
```

- Tilted planes (not bare z) so the VS warp matches the classic per-vertex `ZatPoint` derivation
  exactly, including slanted flats. `GetPlaneTexZ` values during the frame are the interpolated
  ones — the record never carries prev/current.
- **Light slots carry raw ingredients, not derived effective light (amended by ticket 07,
  section 6).** `lightlevel` is the raw `sector->lightlevel` (0..255); `planeLight[2]` are the
  floor/ceiling secplane Light offsets; `flags` bits 1/2 carry the per-plane ABSLIGHTING state. The
  VS derives the classic per-plane effective light from these through the ported light chain
  (section 6). A surface reads the record of the sector its plane refs point at (model sector for
  fake flats), matching the transfer-light sector semantics. `colormap[9]` is the HW `FColormap`
  the fire/pulse/damage thinkers write per tick. The glow slots keep 05's fields and sentinels
  (0 = texture-glow fallback, ~0u = disabled), shifted for alignment by the re-slot.
- **Amended by ticket 07 (section 6).** 05's `colormap` uint32 was annotated "selfmap" — that was
  wrong: the HW path never reads `selfmap` (a software-renderer field); 07's approved amendment
  replaces it with the real 9-byte HW `FColormap`. 05's `skyOffset[2]` slot (at 76) is now
  **reserved** (07's round-3 sky split moves sky scroll to a per-level per-frame uniform — section
  6). The record size is unchanged (96 bytes); the per-frame full-copy write locked in this
  section stands as written.
- *Historical note:* ticket 05 slotted these words the original way — `light[3]` per-plane
  *effective* light at 48, `colormap` (uint32, "selfmap") at 56, `skyOffset[2]` at 76, `reserved[2]`
  at 84; ticket 07's approved amendment re-slotted 48–91 to the raw ingredients above.

### Update protocol

**Load.** Build the CPU record array (`num_sectors × 96B`) and the persistent ring
(`HW_MAX_PIPELINE_BUFFERS` copies; Vulkan mapped memory / GL persistent-mapped — backend mechanics
are section 7's (ticket 08)). Mark load-dynamic sectors: scroll specials (floor/ceiling),
transdoor/alpha flats → their surfaces get the dynamic flag in the surface record. Fake-flat
surfaces referencing a potentially-moving model get the dynamic flag at load too (their plane refs
track the model sector).

**Per frame** (levelmesh frame setup, after `DoInterpolations`, inside `D_Render`'s action):
1. **Pack.** Iterate `Level->sectors`; read planes (`floorplane`/`ceilingplane`), scroll
   (`planes[].xform`), light raw ingredients (`lightlevel`, per-plane Light offsets, `Colormap`),
   glow, transdoor → write the current ring slot (full copy).
2. **Diff.** Compare each record's plane+scroll words against the cached previous-frame copy.
   First change → sector `moved` this frame; two consecutive unchanged frames → sector `settled`.
   (A moving plane changes every frame at any speed, so N=2 is sufficient.)
3. **Lifecycle** (per section 3, ticket 04): a *static* surface whose plane refs include a `moved`
   sector → flip its dynamic flag in the surface record (and rewrite its vertices' dual-purpose
   slot: `(baked z, baked v)` → `(vparam, 0)`). A *dynamic* surface whose referenced sectors are
   all `settled` → **re-bake**: write the current planes' z back into its vertex-pool range,
   restore the dual-purpose slot, clear the flag.

**Write rules (pipeline hazard):** the per-frame full copy touches only the *current* slot — that
is the ring's purpose. Rare small writes (re-bake z patches, first-movement slot rewrites,
surface-record flag or texture-index changes) are applied to **all** ring slots (≤ 4 small
sub-uploads) so in-flight frames never read half-updated state. `RestoreInterpolations()` runs
after frame setup; nothing further is coupled to it. Non-interpolated frames (cutscenes,
`r_NoInterpolate`) pack raw tick values, exactly as the classic path renders them.

**GPU side.** For a dynamic surface the VS: `SectorState s = state[slot][surface.planeRef.sector]`;
`z = ZatPoint(s.plane, vertex.xy)` (plus the transdoor z-1 offset when flagged); `uv += s.scroll`;
light/colormap/glow come from `s` (shading model: section 6, ticket 07). Static surfaces never
touch the buffer. The per-frame slot index is a single uniform.

### Helion alignment (vs. `research/01-helion-levelmesh.md`)

| Aspect | Helion | Levelmesh (locked) |
|---|---|---|
| Per-sector GPU state buffer, sampled per vertex | yes (light buffer) | yes (full record) — **aligned** |
| Light changes never touch vertices | yes | yes — **aligned** |
| Load-time dynamic marking (scroll/alpha/transfer) | `StaticDataApplier` | same, from sector specials — **aligned** |
| Move-start → flag + leave static; move-complete → re-bake | `World_SectorMoveStart/Complete` | snapshot diff → flip / N=2 settled → re-bake — **aligned in effect**, detected differently |
| Streaming | dirty-only (3 floats per changed sector) | full per-frame copy — **deviated** (bandwidth negligible; A/B determinism) |
| Tick interpolation | prev+current in dynamic VBO, VS mixes | single current value, CPU-interpolated — **deviated** (`DoInterpolations` already blends; exact classic parity) |
| Dynamic storage | second per-plane dynamic VBO | none — VS warp from the state buffer — **deviated** (per section 3, ticket 04) |

### Handoffs to dependent sections

- **Section 3 (ticket 04, geometry/lifecycle):** this section drives the lifecycle section 3
  locks — the `moved`/`settled` diff above is the flag-flip and re-bake trigger (the "settled"
  event section 3 requires), and the write rules order the first-movement flag flip against the
  sector's first state-buffer upload. Section 3's "dirty-sector stream" requirement is met by the
  full per-frame copy instead (ADR 0003).
- **Section 6 (ticket 07, lighting/sky):** consumes the record's light, colormap, glow, and
  transdoor fields in its per-vertex shading chain; its approved amendment re-slots the light
  slots (see the record note above).
- **Section 7 (ticket 08, draw organization):** owns the ring's backend storage mechanics (Vulkan
  mapped memory / GL persistent-mapped) and identifies this ring as the render handoff ring — the
  per-frame sector-state slot is part of its frame record, and its render worker consumes the
  frozen slot contents. The snapshot pass runs where section 7 places the frame build: game
  thread, inside the interpolation window; 08's later introduction of an async render worker does
  not move it (the "no async render thread" observation above describes the existing HW path)

## 5. Portal & sector_link rendering (ticket 06)

**What this section locks.** Ticket 06 (grilling; resolved 2026-10-04) locks how the levelmesh path
renders through portals and `sector_link` at full parity: the static region partition of the level,
the semantics of the region record's `transform` / `frontFace` / scissor-stencil slots (container
locked in section 3), the per-portal view-transform mechanism, the visibility protocol (GPU occlusion
queries with 1-frame-ahead prefetch of a predicted candidate set), recursion / re-entry handling, and
the full per-frame portal render sequence. The parity target is the classic path's portal flow
(`research/03-portal-flow.md`): the classic path never transforms or re-meshes portal geometry — it
re-runs the identical traversal in a child drawinfo with a **remapped viewpoint**, LIFO after the
opaque pass, with stencil as the only pixel-level region separator over a shared depth buffer.
Helion's flood-fill + fake-wall approach (research 01) was rejected as the parity path by ADR 0001.

### Region model

**Rationale.** Region-scoped portal views. The level is statically partitioned by portal structure at
build time; each portal view draws only its target region, bounding per-portal vertex cost and keeping
per-frame CPU off level size (the classic path re-traverses the whole level per portal —
`research/03-portal-flow.md`).

**Partition algorithm (build time).** One global flood-fill over the sector graph, run at section 3's
build-order step 3 ("Assign regions (section 5)"):

```text
for each unvisited sector:
	start a new region
	flood the sector graph:
		two sectors are adjacent iff they share a two-sided line
		that is NOT a portal
		- portal lines are barriers
		- one-sided lines never connect
	assign every reached sector (and its subsectors) to the region
```

Each connected component is one region. The **main region** is the component containing the player's
start; the rest are **portal regions**.

**Portal targets.** A portal's **target** is fixed at build time from the destination line + the portal
transform: for line portals, the region *behind* its destination line; for `sector_link`, the region
of its destination sector.

**View scoping.**

- The **main view** draws only the main region. **Each portal view** draws its target region plus
  nested sub-views.
- The portal line's window wall belongs to the **main region** — it is the visible frame in the main
  view.
- **Fake flats / 3D floors / `sector_link` destinations** need no special mechanism: they are extra
  surfaces referencing their model sector (section 3) and sit in the subsector's own region, so the
  region-scoped view covers them (section 4 likewise locks no special state mechanism for them).
- **Re-bake** stays per region (section 3) and is shared across every view that draws that region —
  no per-portal copies.

### Region record slots (section 3's container)

Section 3 locks the region record container and defers the semantics of its view-related slots to this
section.

**`transform` — the remapped viewpoint.** For the main region: identity. For a portal region: the view
transform of the portal that targets it — for a line portal, rotation by the portal's angle diff +
translation to the destination line + a **z-rebase**; for `sector_link`, a pure displacement. The
rotation / translation / angle terms derive from the portal line pair's geometry (build time); the
z-rebase is taken from the **current** sector heights and evaluated **at runtime** (heights move) —
per-frame sequence step 2 below. Parity holds because `DoInterpolations` writes tick-blended heights
before render (section 4), so the portal's current-height sampling matches the classic per-frame
behaviour exactly. Nested portal views **compose** their transforms (parent ∘ child); sky / skybox
views use the same composed-transform mechanism (below). The per-frame evaluated values (z-rebases,
composed transforms, one per visible view) are part of the per-frame view state section 7 streams to
the draw path — the backend plumbing is section 7's; this section locks the semantics only.

**`frontFace` — CCW | CW.** The front-facing winding for drawing that region's sub-ranges under its
view: CCW for the main region and for portal views whose (composed) transform has positive
determinant. A portal view whose transform has **negative determinant** is *mirrored* — the transform
flips winding — and MUST draw its target region with **`frontFace = CW`** (section 3's record requires
exactly this for mirrored portal regions). The face follows each view's composed transform — a region
targeted by several portals is drawn with the face of the view actually drawing it — while the record
slot carries the region's default (unmirrored) value.

**`texRangeOffset` / `texRangeCount` — per-texture sub-range table.** The region's static draw table,
built at load (section 3's build-order step 3) and the **sole draw source** of the levelmesh path
(section 7): one entry per (region, texture, bandIndex) — the (region, texture) sub-range is band 0;
further entries are section 6's band splits for walls in 3D volumes (sub-range k contains the quads of
walls having ≥ k+1 bands, in pool order, re-referencing the same vertex pool) — **sorted by texture**
within the region (one material bind per (region, texture); band sub-ranges within a texture group
differ only by the `uBandIndex` uniform). An entry is the contiguous IBO index range
`{ textureIndex, iboOffset, iboCount }` that one sub-range draw emits; it also carries a precomputed
world AABB for section 7's per-frame cull walk. The region's table is walked per frame when a view
that draws the region is active — the main region always; a portal region only for portals whose
previous-frame occlusion query (above) passed.

**Scissor / stencil state.** The clip state for drawing that region's sub-ranges in a portal view: the
stencil level is the portal's nesting depth (a nested portal draws inside its parent's view at level
+1 with the composed transform), and the level is what masks the portal's sub-range draw and its
interior depth clear to the window polygon (per-frame sequence step 5), together with the view's
scissor clip. The main region draws at stencil level 0.

### Per-portal view transform

- A portal view is **an instance of one shared mechanism**: a per-portal view transform + **view
  clip** applied to the target region's per-texture IBO sub-ranges (section 3) — not per-portal mesh
  baking, and no per-portal vertex copies (consistent with section 3's storage: one whole-level pool).
- **Line portals:** rotate + translate + angle + z-rebase from *current* sector heights (runtime; see
  the `transform` slot above).
- **`sector_link`:** pure displacement.
- **Nested portals compose** transforms (parent ∘ child).
- **View clip:** a portal view draws its sub-ranges clipped to the destination line/plane (per-frame
  sequence step 5).
- **Sky / skybox** are instances of the same portal-view mechanism; this ticket locks the mechanism,
  and ticket 07 owns sky texturing / effects (section 6: sky is ordinary geometry, and the skybox
  composes the portal view with the sky's own transform there).

### Visibility: GPU occlusion query, 1-frame-ahead prefetch

- **Query:** per-portal depth-only draw of the **window polygon** (under the main view) with a
  depth-pass query; result > 0 = visible.
- **1-frame-ahead prefetch:** the queries issued in frame N (after the main pass, tested against the
  main region's depth) gate frame N+1's portal draws. The result is inherently one frame stale.
- **Candidate set = predicted set:** frame N+1's camera = current camera + measured camera velocity ×
  dt; the candidate set is the portals inside the **predicted frustum** (generous margin / widened
  FOV). The velocity is the measured per-frame camera delta, and the frustum is generous to cover
  fast camera moves.
- **Misses:** a portal the prediction misses (fast camera move) is drawn once it enters the candidate
  set and its query passes — a bounded 1–2 frame delay, accepted.
- **Gating:** the query gates both the portal draw *and* its depth clear — an invisible portal gets no
  depth clear, so no spurious un-occlude.
- **Deferred to section 7 (ticket 08):** the query objects (Vulkan occlusion query pool vs GL33
  depth-pass query object) and the window-poly buffer plumbing are 08's territory; this section locks
  the protocol, not the backend mechanism.

### Recursion and re-entry

- The classic caps are preserved (max portal nesting depth, max portals per frame), plus a
  **per-frame visited-set** to break cycles.
- **Draw order:** main pass first, then portals in **LIFO / registration order**; a nested portal
  draws inside its parent's view (stencil level +1, composed transform).

### Per-frame portal render sequence

Locked by the ticket; runs in the levelmesh frame setup (section 7's frame build, inside the
interpolation window):

1. `DoInterpolations` → snapshot pass (section 4).
2. Per-portal transforms (line: rotate/translate/angle + z-rebase from current heights;
   `sector_link`: displacement).
3. Draw the main region (all main-region × texture IBO sub-ranges, main view, z-buffered) — fills the
   depth buffer.
4. Issue occlusion queries for the predicted candidate set (depth-only window-polygon draw, main
   view) — gates the *next* frame.
5. For each portal visible per last frame's query (LIFO / registration order; recursion +
   visited-set): stencil increment on the polygon → clear depth inside (depth-buffer portals only;
   sky/skybox need none) → draw the target region's sub-ranges under the portal view transform
   (composed with any parent) + view clip (destination line/plane), stencil-masked → recurse nested
   sub-views → stencil decrement + depth restore.
6. Sprites (section 7), z-buffered against the levelmesh.

### Handoffs to dependent sections

- **Section 3 (ticket 04, geometry data model):** consumes this section's region partition (its
  build-order step 3, "Assign regions (section 5)"), the `transform` / `frontFace` / scissor-stencil
  semantics this section fills, and the per-region re-bake shared across every view that draws the
  region. Mirrored portal regions set `frontFace = CW`.
- **Section 4 (ticket 05, sector state):** the snapshot-pass ordering (after `DoInterpolations`) is
  what makes step 2's current-height sampling match the classic per-frame behaviour; no state
  mechanism is needed for fake flats / 3D floors / `sector_link` destinations (surfaces sample the
  record of their referenced model sector).
- **Section 6 (ticket 07, lighting / sky):** sky and skybox are instances of the portal-view
  mechanism locked here; 07 owns sky texturing / effects and composes the skybox view with the sky's
  own transform.
- **Section 7 (ticket 08, draw organization):** implements this section's deferred backend pieces —
  the occlusion query objects and the window-poly buffer plumbing — and streams the per-frame view
  state (per-portal composed transform, `frontFace`, scissor/stencil) that step 5's draws consume;
  a draw is one (region, texture, bandIndex) sub-range — (region, texture) is band 0 (section 6).

## 6. Lighting & visual effects (ticket 07)

**What this section locks.** Ticket 07 (grilling, 3 rounds; resolved 2026-10-04, every branch
explicitly chosen/confirmed by the user) locks the levelmesh path's lighting and per-sector visual-
effect model at full parity with the classic path's look. The shading model is fixed: the classic
per-surface light chain (effective light → RescaleLightLevel → CalcRelLight → CalcLightLevel →
CalcLightColor → vColor + softlight/fog/desat/glow) runs **in the levelmesh vertex shader** as a
formula-for-formula, **bit-exact GLSL port** (int32 on the integer paths, the C++ op order in
float32 on the rest; the fake-contrast rel is baked CPU-side at build — no GPU atan). The fragment
shader is the existing `main.fp` with a **define-level adaptation** promoting the per-draw
light/fog/glow/desat values to per-vertex varyings (≈12 vec4, inside the GL33 limits). **Every
surface fetches its sector state record for shading** — not only dynamic ones (light thinkers mutate
static geometry too). Sky is **ordinary geometry** (frontsector light + colormap; the only dynamic
sky state is the per-level per-frame `levelSkyPos` triple). Parity is by construction — same
equations, same inputs, same fragment code — and is verified with the A/B tool at **zero tolerance**
(section 9). Two approved amendments re-slot the record layouts: section 3's surface record grows
48 → 64 B and section 4's sector-record light slots become raw ingredients (both amended in place;
cited here as the amendment source).

### Locked decisions

**Light chain placement: ported into the levelmesh vertex shader (round 1).**

**Why the CPU cannot pre-compute:** a levelmesh draw is a (region, texture, bandIndex) IBO
sub-range (section 7; (region, texture) is band 0) spanning many sectors and sides — the classic's per-draw `SetColor`/`SetFog` state has
nowhere to live. Every per-surface light input is either per-sector-per-frame (light thinkers,
fire/pulse/damage colormaps) or per-surface-static (side tier lights, fake contrast) — exactly the
two sources the VS already fetches: the sector state record (section 4) and the surface record
(section 3).

**Contract.**

- The levelmesh VS computes per vertex the full chain the classic computes per draw (port list
  below) and emits: all `main.vp` varyings (vColor, vTexCoord, vLightmap, vWorldNormal,
  vEyeNormal, pixelpos, glowdist, gradientdist) **plus the promoted per-surface values**: vLightLevel,
  vFogDensity, vLightFactor, vLightDist, vDesaturation, vFogEnabled (int), vGlowTopPlane,
  vGlowBottomPlane, vGlowTopColor, vGlowBottomColor — ≈45 float units ≈ 12 vec4, inside the GL33
  varying limits.
- The levelmesh fragment = `main.fp` + an adaptation block `#define`-ing each promoted uniform to
  its varying (the GL backend already does exactly this for `uLightLevel` → `uLightAttr.a`; the
  pattern is established). All per-pixel lighting math in `main.fp` (Doom lighting equation, fog,
  glow falloff, lightmaps, dynamic lights) is untouched.
- **Every surface fetches the sector state record for shading — not only dynamic ones.** Section 3
  already states "light by all surfaces"; this makes it explicit. Light thinkers
  (strobe/pulse/fire/glow) mutate arbitrary sectors without moving geometry, so static surfaces must
  read the record too. For a dynamic surface the same fetch serves the warp. Bandwidth: 96 B/vertex,
  L1/L2-resident (50k sectors ≈ 4.8 MB), good locality from the per-region/per-piece pool order
  (section 3); sections 9/10 validate at scale.
- C++ remains the single source of truth for the equations; the GLSL is a translation. The A/B tool
  (section 9) is the acceptance gate, zero tolerance.

**Port list (C++ reference → levelmesh VS), in classic order:**

1. Flat effective light: `sector_t::GetFloorLight/GetCeilingLight` (p_sectors.cpp) —
   `ABSLIGHTING ? planeLight : hw_ClampLight(lightlevel + planeLight)`; the lighting sector is the
   one the surface's `lightRef` (section 3) points at (transfer-light / heightsec semantics).
2. Wall base light: `side_t::GetLightLevel` (p_sectors.cpp:1579) — ABSLIGHTING global/tier
   branches, fake-contrast `rel` computed and *returned separately*, trailing `Light +
   TierLights[which]` add gated on `!is3dlight && !ABSLIGHTING && !TIERABS && (!foggy ||
   LIGHT_FOG)`. `foggy = (frontsector->Colormap.FadeColor != 0) || (Level->flags &
   LEVEL_HASFADETABLE)`.
3. `CalcRelLight` (hw_walls.cpp:2100) — the 3-branch rel → rellight.
4. `RescaleLightLevel` (hw_lighting.h:35) — r_extralight cvar.
5. Weapon/extra light: `getExtraLight` (hw_lighting.h:51) = `r_viewpoint.extralight *
   gl_weaponlight`. Flats add it as the whole rel; walls add it to the fake-contrast rel
   (hw_walls.cpp:208 `int rel = rellight + getExtraLight()`).
6. `CalcLightLevel` (hw_lighting.cpp) — the ELightMode branches (dark-mode integer path incl.
   RoundHalfEven(1.87f*L)/5 and the 20-clamp; the software-lighting path) → the uLightLevel value.
7. `CalcLightColor` (hw_lighting.cpp) — integer colormap math (LightColor/BlendFactor/Desaturation)
   → vColor.
8. `GetFogDensity` + `SetFog`/`SetShaderLight` (hw_setcolor.cpp:59,93) — distfogtable (256×2 f32
   uniform buffer, rebuilt on `gl_distfog` change), MAXDIST = 32*r_visibility, the cullcolor branch
   (r_distance_cull_type > 1), outsidefog, fullbright, gl_fogmode.
9. uFogEnabled encoding (vk_renderstate.cpp:356-372): 0 = no fog; black fogcolor → +gl_fogmode
   (1 = view-depth fade, 2 = true distance); non-black → −gl_fogmode. Levelmesh: computed per
   vertex into vFogEnabled; the main.fp branches already key off it.
10. The 3D-light variants (band split / FF_FOG — below).

`ELightMode` is per level (`Level->info->lightmode`, `gl_maplightmode`, `gl_lightmode` →
`getRealLightmode`, g_level.cpp:151) → per-level uniform; `uPalLightLevels`/numShades → global
uniforms.

**Sector-record light re-slot (round 1, approved; amends section 4, ticket 05).** 05 locked the
slots and the per-frame write, and deferred the light *semantics* to 07. 07 replaces the derived
"effective light" slots with **raw ingredients** (the VS derives them): the record is 96 bytes,
unchanged size, now `lightlevel` (raw `sector->lightlevel`) + `planeLight[2]` (secplane Light
offsets) + the 9-byte `FColormap` (LightColor/BlendFactor/Desat/FadeColor/FogDensity) + per-plane
ABSLIGHTING flag bits; the amended layout is in section 4. 05's `colormap` uint32 was annotated
"selfmap" — that annotation was wrong: the HW path never reads `selfmap` (a software-renderer
field). 05's `skyOffset` slot is **reserved** (the round-3 sky split below).

**Surface-record growth 48 → 64 B (round 1, approved; amends section 3, ticket 04).** 04's
documented offsets contained an 8-byte hole (`planeRef[4]` spans 8..24 but `lightRef` was listed
at 32); 07 normalizes the base layout and appends a 16-byte wall-shading/sky block — the final 64 B
record (side `Light`/`TierLights[tier]`, the baked fake-contrast rel pair, `lightlistRef`, band
refs, the extended flag bits incl. tier, lightmapNum, skyScrollKind, m2snf) is in section 3.

**Fake-contrast bake.** `rel = f(line delta, Level->WallHorizLight, Level->WallVertLight)` is
static (smooth: `RoundHalfUp(WallHorizLight + fabs(atan(dy/dx)) / 1.57079 * (Vert - Horiz))`,
delta.X != 0; classic: delta.X == 0 → Vert, delta.Y == 0 → Horiz, else 0 — p_sectors.cpp:1579).
Both values are baked CPU-side at build into the surface record's `relSmooth`/`relNonSmooth`
fields; there is no GPU atan. VS selection: `smooth = LEVEL2_SMOOTHLIGHTING || side smooth ||
r_fakecontrast == 2`; rel applies only when `(!foggy || LEVEL3_FORCEFAKECONTRAST) &&
!NOFAKECONTRAST && r_fakecontrast != 0`; rellight = `CalcRelLight(L, org, rel)` with org = raw
(unrescaled) frontsector lightlevel.

**3D floor lights: per-level light state buffer + band split (round 2).**

**Light state buffer** (per level, full-copied per frame like section 4's state buffer): one 12 B
record per `Level->lightlist` entry — `uint16 lightlevel` (raw `*p_lightlevel`), `FColormap` 9 B
(the light's `extra_colormap`, re-copied at runtime by `P_RecalculateLights`), 1 B pad. These are
live values (pointers into source sectors; A_ChangeSector colormap propagation), so nothing is
baked.

**3D-floor flats:** the surface's `lightlistRef` (baked at build — `P_GetPlaneLight` resolution is
static topology) → VS: `L = RescaleLightLevel(light.lightlevel)`; colormap = the light's (FF_FOG
flag: LightColor := light.FadeColor, other components 0, white FlatColor — hw_flats.cpp
SetFrom3DFloor). Flats do not split.

**Walls in a 3D volume = per-band sub-draws** (the classic splits them, hw_walls.cpp:305-325): the
wall quad is drawn once per intersecting 3D-floor layer, each clipped to that layer's z-band via
`uSplitTopPlane`/`uSplitBottomPlane` (main.vp:79-82 clip distances; the planes are the 3D-floor
plane + next-layer plane / front floor — dynamic, from the sector state records). Levelmesh
expression:

- Build time: enumerate the intersecting 3D-floor layers per wall (static structure — section 3's
  decision 4) → **M band records** in a per-level static **band table**: `{ int16 lightRef; int16
  reserved; uint32 splitTopPlaneRef; uint32 splitBottomPlaneRef }` (16 B). Split refs are section 3
  plane refs into the model sectors; `lightRef = -1` means the band's light has no caster → use the
  wall's own light (the classic `caster == nullptr` case). The base surface record keeps the tier
  statics and points at its band range (`bandOffset`/`bandCount`).
- IBO: sub-range granularity becomes (region, texture, **bandIndex**) — section 7 consumes.
  Sub-range k contains the quads of walls having ≥ k+1 bands, in pool order. The vertex pool is
  unchanged (bands re-reference the same quads; only IBO indices repeat).
- VS (band k): light = `band.lightRef >= 0 ? RescaleLightLevel(lightBuf[i].lightlevel) : the
  wall's normal chain`; colormap = the light's FColormap **with the wall's own FadeColor/FogDensity**
  (the classic merges `thiscm.FadeColor/FogDensity = Colormap.*`, hw_walls.cpp:315-317); split
  planes = the sector-record planes of splitTop/BottomPlaneRef → ClipDistance3/4 exactly as
  main.vp:81-82.

**Parity strategy: bit-exact port + A/B, zero tolerance (round 2).**

- Integer paths (CalcLightColor, RescaleLightLevel, CalcRelLight, CalcLightLevel's Doom-dark
  integer branch, all clamps) use int32 arithmetic — bit-exact.
- Float paths (the 1.87f dark-mode product + RoundHalfEven, GetFogDensity, the distfogtable f32
  lookups, sky scroll adds) replicate the C++ op order in float32 — bit-exact under IEEE-754
  (both sides float32).
- distfogtable (256×2 f32) is a uniform buffer rebuilt on `gl_distfog` change; fake contrast is
  baked (no GPU atan); skytransfer resolution happens at build.
- Verification: A/B tool, zero tolerance, on the checklist below plus the cvar sweep.

**Sky: ordinary geometry (round 3).**

Verified classic facts: sky is shaded like ordinary geometry (frontsector light + colormap; no
special light level). The dynamic per-frame sky state is just three per-level floats
(`Level->hw_sky1pos / hw_sky2pos / hw_skymistpos`, r_sky.cpp:146, advanced by R_UpdateSky).
`sector->skytransfer` is a **static int** (MBF: 0 or `(lineIndex+1) | PL_SKYFLAT` — the sky texture
comes from that line's sidedef with angle = texture X offset × 360/65536, y offset, mirror flag;
resolved by `HWSkyInfo::init`, hw_sky.cpp:56).

**Contract.**

- Sky surfaces (section 3 flag bit 1) resolve at build time exactly as `HWSkyInfo::init` does
  (which sky texture — incl. the sky2/doublesky selection from LEVEL_SWAPSKIES/LEVEL_DOUBLESKY/
  PL_SKYFLAT and the MBF line-texture case) → the static parts (texture, angle offset, y offset,
  mirror) fold into the pool's baked u/v and textureIndex (the existing static bake handles them);
  the surface record carries `skyScrollKind` / `skyScrollKind2` (which of the three per-level
  scroll positions drives the surface, and the doublesky second layer).
- VS: `u += levelSkyPos[skyScrollKind]` (+ second layer for doublesky), with `levelSkyPos =
  (hw_sky1pos, hw_sky2pos, hw_skymistpos)` a per-level per-frame uniform triple. This is the locked
  round-3 split: **no per-frame sector-record write for sky scroll** — section 4's `skyOffset`
  slot is reserved, and scrolling-sky levels churn zero record bytes.
- Sky fadecolor rule (hw_sky.cpp:116-126) as a VS branch on sky surfaces: `r_distance_cull_type >
  1` → `FadeColor > 0 ? FadeColor : gl_cullcolor`; else `Level->skyfog > 0` → the sector FadeColor;
  else none.
- **Skybox** (PORTS_SKYVIEWPOINT/HORIZON/PLANE, GLSector_Skybox) = a section 5 portal view:
  ordinary flat/wall shading inside, plus the inkybox fog-density ×1.5 boost and cullcolor as
  per-view uniforms.

### Locked by code investigation (no decision needed)

- **Glow:** `sector_t::GetWallGlow` (p_sectors.cpp:1251) — sector `GlowColor` (sentinels: 0 =
  fall back to the *texture's* glow, ~0u = disabled) + `GlowHeight`; the shader receives color+
  height (height as the vec4 alpha) and the glow planes are the frontsector's **actual (tilted)**
  floor/ceiling planes (hw_walls.cpp:45-51, 210-214). Levelmesh: the sector record carries
  color+height (sentinels intact, section 4); the planes come from the same record fetch; the
  texture-glow fallback uses a per-texture glow table (glowColor u32 + glowHeight f32 per texture,
  built at load, refreshed by the snapshot pass for animated glow frames). Glow is **walls only** —
  the HW classic has no flat glow; the levelmesh does not add one.
- **Dynamic lights (dlight_t):** per-pixel fragment work against the global per-frame light buffer
  (`ProcessMaterialLight`, main.fp) — traversal-independent, unchanged. The level pass uses
  `uLightIndex = 0` (the classic never sets it for walls/flats; decals/sprites keep their own
  indices on their existing path).
- **Selfmap:** a software-renderer field — never read on the HW path; no levelmesh analogue.
- **Animated flats:** section 3's mutable textureIndex (snapshot pass advances the frame); no 07
  involvement.
- **Scrolling:** section 4's per-plane scroll + VS `uv += scroll` (scroll sectors are load-dynamic
  per section 4).
- **Transdoor:** section 4's flag + the classic z-1 floor offset in the VS.
- **Weapon light:** per-view (`r_viewpoint` is per player in net) — per-view uniform.
- **culldist / fullbright / inkybox:** per-view/per-level uniforms as listed below.
- **WALLF_EXTCOLOR gradients, TextureFx, NPOT emulation:** the texture-manipulation domain (section
  7 / texture handling); the gradient planes they need are already in the sector record.

### Uniform set (spec-ready)

- **Global (rebuilt on cvar change):** distfogtable[2][256] f32; uPalLightLevels (lightmode pack);
  numShades.
- **Global per-frame:** the dynamic-light buffer (the classic lightbuffer — unchanged).
- **Per level (static):** lightmode (ELightMode int); outsidefog, outsidefogdensity, sector fogdensity
  (Level->fogdensity), skyfog. (The level *flags* — HASFADETABLE, SMOOTHLIGHTING,
  FORCEFAKECONTRAST, DOUBLESKY, SWAPSKIES — are baked at build, not uniforms.)
- **Per view (dynamic):** r_visibility (→ MAXDIST = 32*r_visibility), r_distance_cull_type,
  gl_fogmode, r_fakecontrast (mode selector), r_extralight, gl_weaponlight, r_viewpoint.extralight,
  culldist, fullbright-scene flag, inkybox flag (skybox views).
- **Per level (dynamic):** levelSkyPos (3 f32); the 3D-light state buffer (12 B × light count);
  **cullcolor** (1 value) — the fog/clear-color latch written per frame per view by section 8's
  (ticket 11) exposure pass (classic `hw_bsp.cpp:378-392`: `r_distance_cull_type > 0` &&
  `IsDistanceCulled` && nonzero fade color → `Level->cullcolor = FadeColor`; `gl_cullcolor`-initialized,
  latching, never reset per frame). Fog-color selection rule to reproduce (`hw_setcolor.cpp:108`):
  `r_distance_cull_type > 1` → fog color = `Level->cullcolor`.
- **Per surface:** everything else, from the records above.

### Parity checklist (A/B, zero tolerance)

Maps: lighting stress (D2), skybox (D3), lightmap, fog/fade (outsidefog + FadeColor), glow (sector +
texture + animated), 3D floors (multi-layer → band split), animated/scrolling flats, transfer light
(sector_link light), fake contrast (all line angles incl. axis-aligned).
Cvar sweep: r_lightmode 0/1/2/8, gl_fogmode 0/1/2, r_fakecontrast 0/1/2, r_extralight ±,
r_distance_cull_type 0/1/2, gl_weaponlight 0/1, lightmaps on.
Runtime: strobe/pulse/fire/glow/damage thinkers active during the diff runs (frame-locked).

### Handoffs to dependent sections

- **Section 7 (ticket 08, draw organization):** sub-range granularity (region, texture,
  bandIndex); the band table + uBandIndex draw uniform; the per-view uniform list; levelSkyPos;
  fullbright/inkybox per view; the fragment adaptation block (#define promoted uniforms →
  varyings) is the levelmesh shader variant 08 wires up; the per-frame uploads (sector state ring,
  3D-light state buffer, levelSkyPos, cullcolor) stream on 08's frame build.
- **Sections 9/10 (scale/perf):** validate the per-vertex sector-record fetch (all surfaces) and
  the band IBO repetition at slaughter scale.
- **Section 9 (A/B tool):** the checklist above is the acceptance input.

### Amendments (post-resolution, ticket 11 — 2026-10-04)

- **cullcolor is per-level per-frame dynamic**, not static (corrected in the uniform set above).
  Written by section 8's (ticket 11) exposure pass from the culled draw list (frustum-only gate —
  documented delta vs the classic's occlusion gate, D6). Consumers unchanged: view clear color
  (`r_utility.cpp:1294-1295`) and fog color (`hw_setcolor.cpp:108`).

## 7. Draw organization & backend paths (ticket 08)

**What this section locks.** Ticket 08 (grilling, 4 rounds; resolved 2026-10-04, every branch
explicitly chosen/confirmed by the user) locks the levelmesh path's per-frame draw organization and
pipeline integration for both backends (data layer backend-neutral per the standing constraints,
Vulkan first in phasing). Frame organization: the levelmesh path adds a **Vulkan-only render worker
thread** that owns the 3D world pass only — an explicit spec-level deviation from both Helion
(single-threaded, confirmed by primary-source check) and UZDoom's current state (no HW render thread
exists); the game thread keeps the sim, the frame build (inside the interpolation window, replacing
today's `RenderView` call), the 2D stack, and presentation. Handoff: **05's sector-state ring IS the
handoff ring** — the same `HW_MAX_PIPELINE_BUFFERS` slots (2 desktop / 4 Android), in-order processing,
fence-gated back-pressure, no frame skipping. Draw list & culling: the sole draw source is the static
per-region sub-range table — (region, texture, bandIndex), sorted by texture at build (sections 3/5/
6) — walked per frame on the game thread with per-sub-range world-AABB frustum cull, producing a
compact per-slot list with no per-frame allocation. Pass order: 1:1 with the classic `RenderScene`.
Vulkan fit: dedicated `LevelMeshSet` descriptor set + existing `HWBufferSet` + existing per-material
texture sets; new 32-byte vertex format and levelmesh pipeline, purely additive. Record buffers:
uniform texel buffers (`samplerBuffer`) on both backends, locked by constraint. GL33: one program,
the pool as one VBO + one VAO per region, synchronous on the game thread. Sprites stay on the existing
sprite batch path; the job pool stays classic-path only; precache is unchanged. Selection:
`gl_uselevelmesh` (Bool, `CVAR_ARCHIVE | CVAR_GLOBALCONFIG`, default false at handoff) + SettingsPage
entry; the default flip is 09/10's call. The classic path stays fully intact (traversal, job pool,
batch lists) — the component-fate table below. "Render worker (levelmesh)", "Handoff slot", and
"Window polygon" are new glossary terms (section 2).

### Frame organization (rounds 1+3: async render worker, Vulkan-only, 3D pass only)

**Premise correction.** The ticket asked for a "fit into the render thread" (`r_thread.cpp`) — a false
premise: `r_thread.cpp/h` is the software renderer's span-drawer framework, never used by the HW path
(see "Locked by code investigation" below). No HW render thread exists today.

**The decision.** Asked how Helion handles it first: **Helion master is synchronous single-threaded** —
`Client/Client.cs` `Window_MainLoop` from the window frame callback → `RunLogic()` (35 tps ticker) →
`Render()` (command replay, world renderer synchronous) → swap; no render-thread file anywhere in
Client/ or Core/Render/, no frame ring, no render-thread history in RELEASENOTES (primary-source
check). The user then **chose to introduce an async render thread anyway** — an explicit spec-level
deviation from both Helion and UZDoom's current state.

- **The worker owns the 3D world pass only**: per-frame uploads (sector-state ring slot, 3D-light
  state, levelSkyPos, sprite/model vertex slots, window-poly buffer), the full levelmesh 3D issue
  (main-region sub-ranges, models, decals, portal views, sprites, occlusion-query issue — pass order
  below) into the screen's render target, and a per-slot completion fence.
- **The game thread keeps** the sim, the frame build (inside the interpolation window), the 2D stack
  (ZWidget statusbar, menus, console, VM `CallDraw` — coupled to live game state, not a command list),
  and presentation. The fence wait lands where today's `RenderView` call happens (the `D_Render`
  lambda, d_main.cpp) — 2D composition and `End2DAndUpdate()` are untouched.
- **Vulkan only.** The GL33 levelmesh path issues on the game thread (synchronous, as today). A shared
  second GL context for GL33 async is explicitly out of scope / future work (GL calls are thread-bound
  to the context owner; Vulkan queue submission is thread-safe by design).

**Correctness invariant (the heart of the spec):** everything that reads the single-buffered
interpolated playsim state (sector planes, actor positions, light state, sky positions) runs on the
game thread **between `DoInterpolations` and `RestoreInterpolations`** — the frame build replaces
today's `RenderView` call inside the `D_Render` lambda. The worker consumes frozen ring-slot contents
only; it never touches playsim state. Section 4's cross-reference is exactly this: the snapshot pass
runs where this section places the frame build (game thread, interpolation window); the worker does
not move it.

**What the worker buys (stated honestly):** today's Vulkan submissions are already async, and frame
N+1's CPU build already overlaps frame N's GPU execution via the `HW_MAX_PIPELINE_BUFFERS` ring. What
the worker removes from the game-thread critical path is the **issue + upload CPU cost**: the per-frame
sector-state ring upload (full per-level copy — e.g. 50k sectors × 96 B ≈ 4.8 MB), sprite/model vertex
buffer updates, light state + levelSkyPos, window-poly buffer, descriptor updates, query issue, and
queue submit (driver-side stalls). It is also the architectural precondition for any deeper async
render later (05's slot ring already has the shape).

### Handoff slot & back-pressure (round 3)

- **05's sector-state ring IS the handoff ring** — no second ring. `HW_MAX_PIPELINE_BUFFERS` slots (2
  desktop / 4 Android — buffers.h:32/36, the same constant the existing per-frame `mBufferPipeline`
  buffers use). The per-frame sector-state slot section 4 streams is part of the frame record below,
  and the worker consumes the frozen slot contents.
- **Slot contents (the frame record):** sector-state records (05, 96 B/sector), 3D-light state (07,
  12 B/light), levelSkyPos triple (07), per-viewpoint values (camera pos/rot, fov, extralight, fog
  params — 07's per-view uniform list), the portal-view list (06: portal index, target region, view
  transform, query-gate flag, skybox/inkybox flag), the **culled draw list** (main-region sub-range
  entries + per-portal-view entries, below), sprite + model vertex slot references (the existing
  per-frame `mBufferPipeline` vertex ring — filled on the game thread as today), and the query results
  to apply (previous frame's, below).
- **Lifecycle:** the game thread builds into the oldest free slot (blocks on the oldest in-flight
  slot's fence if none is free — back-pressure); marks it ready; signals the worker. The worker
  processes slots **in order** (no frame skipping: the game thread 2D-composes per frame in order, so
  every built frame's 3D pass must complete — stale-frame drop is impossible in this architecture).
  The worker waits on the slot's GPU-completion fence before reusing it, then uploads + records +
  submits.
- Concurrency: desktop (2 slots) = 1 in build + 1 in flight — one frame of overlap, the maximum this
  architecture allows. Android (4) = two in flight.

### Per-frame frame-build sequence

The concrete per-frame ordering, assembled from the pieces locked above and in sections 4/5/8 (the
3D-pass half is section 5's per-frame portal render sequence, issued in the pass order below):

**Game thread — frame build** (inside the interpolation window, the `D_Render` lambda):

1. `DoInterpolations(I_GetTimeFrac())` (existing).
2. Snapshot pass (section 4) — full sector-state copy into the current ring slot, diff, lifecycle
   (flag flip / re-bake).
3. Per-portal transforms (section 5 step 2: line rotate/translate/angle + z-rebase from current
   heights; `sector_link` displacement) and the window-poly buffer update (section 5's static
   endpoints + current heights of both portal-adjacent sectors — the same heights the transforms
   read; GL33: here on the game thread, Vulkan: by the worker from the slot).
4. Per-view sprite gather (main view + query-gated portal views; below).
5. Dither tail (section 8, OOB non-portal views): `P_CheckSight` + `SetDitherTransFlags` — the last
   playsim-mutating step (amendment A8 below).
6. Cull walk (below) → compact per-slot culled draw list (plus section 8's exposure marking and
   cullcolor latch handling).
7. Slot handoff: fill the slot's remaining fields (per-viewpoint values, portal-view list, light
   state, levelSkyPos, previous frame's query results with gate flags applied) and mark ready; signal
   the worker.

**Render worker (Vulkan) — per slot, in order:**

1. Wait on the slot's GPU-completion fence (back-pressure).
2. Upload: sector-state ring slot, 3D-light state, levelSkyPos, sprite/model vertex slots,
   window-poly buffer (derived from the slot's sector state + the slot's portal transforms).
3. Record + submit the full 3D pass into the screen's render target in the pass order below —
   including the occlusion-query issue after the main-region pass for the predicted N+1 candidate set
   (predicted N+1 camera = the slot's viewpoint + measured velocity × dt, computed from the slot's
   viewpoints).
4. Per-slot completion fence.

**GL33:** the same frame build on the game thread, and the 3D pass issues synchronously on the game
thread in today's `RenderView` call site's place (the worker steps collapse into the game thread).

### Per-frame draw list & culling (round 1)

- The **sole draw source** is the static per-region sub-range table — section 3's per-region IBO with
  per-texture sub-ranges (semantics: section 5's `texRange` entry), refined by section 6's band split
  to granularity **(region, texture, bandIndex)**. Built at load and **sorted by texture**
  (state-change minimization: one material bind per (region, texture); band sub-ranges within a
  texture group differ only by the `uBandIndex` uniform — no state change).
- **Per-frame CPU walk** (game thread, interpolation window, no per-frame allocation): the
  main-region table is always walked; portal-region tables are walked only for portals whose
  previous-frame occlusion query (section 5) passed. Each sub-range entry carries a precomputed world
  AABB → per-entry frustum test (thousands of AABB tests at slaughter scale ≈ trivial); a region
  early-outs if its own AABB misses. Output: a compact per-slot draw list (sub-range index + vertex
  count; static bound = total sub-range count).
- **Pass order** (replaces the classic `RenderScene` geometry parts; same target, existing
  `FRenderState` state setters):
	1. Main-region sub-ranges — z-buffered, depth write, DF_Less, STYLE_Source alpha cutoff (classic
	   Parts 1+2+3 semantics: opaque, masked-cutoff, masked-OFS with the existing depth bias; section
	   6's band-split draws live here).
	2. Models (existing `GLDL_MODELS` path with bones — unchanged; z-tested against levelmesh depth).
	3. Decals (existing DF_LEqual path — unchanged).
	4. SSAO (unchanged; screen-space, `gl_ssao_portals` budget).
	5. Portal views (section 5: stencil +1, depth clear, target-region sub-ranges under the view
	   transform with the existing `mClipLine`/`mClipHeight` clip, nested recursion, per-view cull-mode
	   flip for mirrored regions).
	6. `GLDL_TRANSLUCENTBORDER` + `GLDL_TRANSLUCENT` `DrawSorted` (depth mask off) — blended level
	   surfaces + sprites, below.
	7. Occlusion-query issue for the next frame's predicted candidate set (after the main-region pass,
	   depth-only window polys, below).

  **Order-independence argument (why texture-grouped order is parity-safe for pass 1):** opaque +
  alpha-cutoff (STYLE_Source, no blend) geometry is order-independent under the z-buffer — the
  classic's own `gl_sort_textures` re-sorting of those lists (hw_drawinfo.cpp:547-551) is a batching
  optimization, not a visual mechanism. Only the blended pass (6) is order-dependent.

- **Blended level surfaces (parity-critical):** the classic draws `GLDL_TRANSLUCENT` (translucent 3D
  planes, translucent flats, translucent models, sprites) with `DrawSorted` (quadtree plane sort —
  `SortNode`/`FindSortPlane`, hw_drawlist.h:55-117), depth mask off, after portals (`RenderTranslucent`,
  hw_drawinfo.cpp:597-611). **The levelmesh feeds its blended surfaces into that exact list as
  per-surface entries** (a surface's pool span = one draw; these are the small blended subset — at
  most dozens of surfaces per map), so the classic's sort algorithm, ordering semantics, and
  depth-mask state are reused verbatim. No deviation, no parity waiver: A/B (section 9) gates this at
  zero tolerance (section 6).

### Vulkan pipeline & descriptors (round 1)

- New **pipeline layout** for the levelmesh pipeline = **HWBufferSet** (existing: per-viewpoint/
  matrix/stream UBOs + light/bone SSBOs — binds as today; the light buffer is genuinely used by
  `main.fp`'s dynamic lights, bones unused) + **LevelMeshSet** (new: the levelmesh-specific global
  buffers, below) + the **existing per-material texture set** (FTextureManager's Vulkan texture sets —
  reused as-is for the per-texture bind).
- **LevelMeshSet** (one new descriptor-set layout, one set per level): surface records (64 B/surface,
  04/07), sector-state ring (one large buffer, HW_MAX_PIPELINE_BUFFERS × level slots; slot selected by
  uniform), 3D-light state (12 B/light, 07), static tables (band table 07, per-texture glow table 07,
  sub-range table + region records 04/06/08).
- **New vertex format** (32 B, 6 attributes: pos3, dual-slot2, u, lightmapUV2, surfaceIndex — 04)
  registered via `GetVertexFormat`; **new pipeline** (levelmesh VS + `main.fp` fragment with 07's
  promoted-uniform adaptation block). Purely additive: classic/raytracer/PP layouts untouched → no
  pipeline-cache invalidation; the new pipeline is a new cache key.
- **Record-buffer mechanism (locked by constraint):** **uniform texel buffers (`samplerBuffer`)** for
  all record buffers on **both** backends — GL 3.3 core has no SSBOs (SSBO = 4.3 core / ARB extension;
  the GL backend compiles 330-core shaders, gl_shader.cpp:457-459), while `samplerBuffer` is core
  since GL 3.2 and native on Vulkan. One shared GLSL (`texelFetch` at integer texel = byte offset / 16;
  every record size is a 16-byte multiple: 64 B = 4 texels, 96 B = 6, 12 B → 16 B padded). A
  Vulkan-side SSBO variant is a 09/10 A/B perf item, not a design split.
- **Render passes:** none new — the levelmesh draws into the existing scene render pass (same
  VkRenderPassKey: depth/stencil, MSAA, draw buffers).

### GL33 draw path (round 2)

- One program (the shared levelmesh VS + `main.fp` adaptation — the same GLSL as Vulkan).
- **Geometry:** the vertex pool as one GL VBO (static; one upload at build; dynamic-surface spans
  re-uploaded on rebake — 05) + **one VAO per region** capturing (VBO, region IBO, 6-attribute layout,
  stride 32). Per draw = VAO bind + `glDrawElements` over the sub-range. Region count is small/static
  → VAO count is trivial.
- **Record buffers:** uniform texel buffer objects per LevelMeshSet buffer (static upload + per-frame
  sub-uploads for sector state / light / sky — game thread, synchronous).
- **State:** existing `FRenderState` (depth test/mask, stencil for portal views, the existing
  `mClipLine`/`mClipHeight` VS uniforms for the section 5 view clip, cull-mode flip for mirrored
  regions); the sector-state ring as per-slot buffers (HW_MAX_PIPELINE_BUFFERS), selected by uniform.
- **Threading:** game thread (round 3); a shared second GL context for async is out of scope.

### Sprites, models & the job pool (round 2)

- **Sprite source (no traversal):** per frame, per active view (main view + occlusion-gated portal
  views), walk that view's **region's actor list** (built at load: actors assigned to their
  subsector's region — section 5's region partition), frustum-test each sprite against that view's
  frustum (camera frustum for main; the portal's view-transform frustum for portal views — the
  classic's portal `DrawContents` sprite semantics), and feed survivors into the **existing sprite
  batch path** (HWSprite, `GLDL_TRANSLUCENT` entries, `DrawSorted` plane sort, OFS variants — all
  unchanged; per-sprite vertex fill on the game thread into the existing per-frame vertex ring as
  today, issue per the backend's threading).
- Sprites inside a portal view draw inside that view's stencil/clip, z-buffered against the portal
  view's geometry (section 5 step 6).
- The visible-sprite list this pass produces is section 8's (ticket 11) input (the RenderedTargets
  replacement for the dither-transparency loop).
- **Job pool** (`renderPool(1)` + `RenderJobQueue`, `gl_multithread`, hw_bsp.cpp:43-51): kept,
  **classic-path-only**. It exists to parallelize per-frame wall/flat vertex generation
  (WallJob/FlatJob) — which the levelmesh eliminates. The sprite gather is a per-view all-actor walk
  (frustum tests are ~ns each) — single-threaded on the game thread. `gl_multithread` is unchanged.
- **Precache** (`hw_precache.cpp`, `gl_precache`): **unchanged** — `PrecacheLevel` (p_setup.cpp) scans
  WAD sector/side/actor data at level load, already renderer-independent; it warms the same textures
  the levelmesh binds. (The ticket's "fate of precache" question is answered by the code.)

### Occlusion query plumbing (round 2)

Section 5 locked the mechanism (per-portal depth-only window-polygon draw with depth-pass queries,
main view; frame N queries gate frame N+1 draws; a miss = bounded 1–2 frame delay). This section locks
the plumbing:

- **Query pool:** per level, one query object per portal (allocated at level load, sized to the
  level's portal count; released at level teardown). Vulkan: a VK_QUERY_OCCLUSION pool (pass count,
  not binary — the count is free and useful for A/B tooling/debug) + per-frame result buffer. GL33:
  depth-pass query objects (depth-only draw with no-op FS, no color write).
- **Window-poly buffer:** small dynamic vertex buffer, one quad per portal (two triangles), updated
  per frame during the portal-transform step — endpoints (static, section 5) + **current** heights of
  both portal-adjacent sectors (the same heights section 5's transforms read; Vulkan: derived by the
  worker from the slot's sector state + the slot's portal transforms; GL33: game thread).
- **Timing (Vulkan worker):** frame N, after the main-region pass — update the window-poly buffer,
  issue the depth-only draw for the **predicted N+1 candidate set** (portals in the generous frustum
  of the predicted N+1 camera = slot viewpoint + measured velocity × dt, computed by the worker from
  the slot's viewpoints), begin the queries. Frame N+1 start: read frame N's results (complete — no
  stall; the one-frame staleness is inherent to section 5's design) and apply the gate flags to the
  slot's portal-view list. GL33: the same sequence on the game thread.
- The query gates both the portal view's draw **and** its depth clear (section 5).

### Selection (round 1)

- `gl_uselevelmesh` (Bool, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, **default false at handoff**). Read at
  level setup → picks the path per map; a mid-game toggle applies at the next map load (same
  semantics as backend switching). The A/B tool (section 9) drives the same cvar.
- **SettingsPage entry** (the LauncherWindow page — used at startup and as the in-game options menu —
  next to the backend radio group): checkbox/radio bound to the cvar. The page currently writes
  startup defaults via FStartupSelectionInfo; the entry additionally writes the cvar live for in-game
  use.
- **Default flip** (levelmesh becomes the default) is gated on 09/10 results — that is 09/10's
  decision, not this spec's.

### Locked by code investigation (no decision needed)

- **The ticket's render-thread premise is false:** `r_thread.cpp/h` (src/common/rendering/) is the
  software renderer's span drawer (DrawerThreads, line splitting, memcpy-to-videomemory); it is used
  by the SW renderer only, never by the HW path. It stays for SW and is unrelated to the levelmesh.
- **Frame flow (HW):** `D_Display` (display-paced; `End2DAndUpdate()` = compose + swap) → `D_Render`
  lambda → `DoInterpolations(I_GetTimeFrac())` → `RenderView` → `RestoreInterpolations` — all on the
  game thread today. The levelmesh frame build replaces the `RenderView` call site.
- **The 3D view is a render target** composed by the 2D stack (`screen->BeginFrame` → 3D → 2D
  HUD/statusbar/menu → `End2DAndUpdate`) — why the 2D stays on the game thread (round 3).
- **Mirror = `HWMirrorPortal : HWLinePortal`** (hw_portal.h:231) — a line portal → automatically under
  section 5's portal-view mechanism (reflection transform + target-region re-draw). No extra work
  here.
- **Shadowmap** (hw_shadowmap.cpp) = GPU ray tests against the play-sim-owned LevelAABBTree —
  traversal-independent, unchanged on both paths.
- **SSAO** (`gl_ssao_portals`, `AmbientOccludeScene`) — screen-space post-process over the rendered
  image with a per-frame portal budget; no level-geometry dependency, unchanged.
- **`gl_sort_textures`** (hw_drawinfo.cpp:547-551) plane-sorts the PLAIN/MASKED lists — a batching
  optimization on order-independent (no-blend) geometry; the levelmesh's texture-grouped order is
  parity-safe for those passes (argument above).
- **Classic pass inventory (for the 1:1 mapping):** `RenderScene` = PLAIN (alpha ≥ 0) → MASKED (alpha
  > gl_mask_threshold cutoff) → MASKEDOFS (depth bias −1, −128) → models (bones) → decals
  (STYLE_Translucent, DF_LEqual); then SSAO; then portals; then `RenderTranslucent` =
  TRANSLUCENTBORDER + TRANSLUCENT `DrawSorted` (depth mask off).

### Component-fate table

The classic path stays fully intact (selectable, the fallback); "removed" means removed from the
levelmesh path's frame, never deleted from the tree.

| Component | Classic path | Levelmesh path | Notes |
|---|---|---|---|
| `hw_bsp.cpp/h` — HWWalk, CreateScene traversal, job pool | kept | removed from frame | traversal CPU scales with level size — the core cost the levelmesh eliminates |
| `hw_walls.cpp/h` — HWWall, per-draw state/light | kept | removed | light chain → levelmesh VS (07) |
| `hw_flats.cpp/h` — HWFlat | kept | removed | same |
| `hw_vertexbuilder.cpp/h` — per-frame plane vertex generation | kept | replaced by 04's static pool build (build time) + 05's rebake sub-uploads | |
| `hw_drawlist.h` — HWDrawList / GLDL_* | kept | kept for sprites + models + decals + blended level surfaces | levelmesh geometry uses no wall/flat lists; blended surfaces feed in as per-surface entries |
| `hw_drawinfo.cpp/h` — CreateScene/RenderScene/DrawScene | kept | replaced by the levelmesh frame build + 3D pass (new module) | DrawScene's shell shape (portal recursion, SSAO, 2D handoff) is preserved |
| `hw_portal.cpp/h` — HWPortal state machine | kept | replaced by section 5's region/portal-view/query mechanism | |
| `hw_clipper.cpp` — CPU BSP clipping | kept | removed (VS clip via the existing mClipLine/mClipHeight uniforms) | |
| `hw_sky.cpp/h` — HWSkyInfo per-frame sky | kept | replaced by build-time resolution + levelSkyPos (07) | |
| `hw_precache.cpp` | kept | kept unchanged | already a WAD scan at load, renderer-independent |
| `hw_shadowmap.cpp`, SSAO, post-process | kept | kept unchanged | traversal/screen-space independent |
| `r_thread.cpp/h` (SW span drawer) | kept (SW renderer) | n/a | unrelated to the HW levelmesh |
| **New** | — | levelmesh draw module (frame build + worker + 3D pass + queries), levelmesh VS + main.fp adaptation, LevelMeshSet/VAO/record-buffer wrappers, `gl_uselevelmesh` cvar + SettingsPage entry | |

### Handoffs to dependent sections

- **Section 4 (ticket 05, sector state):** its ring is the handoff ring (above) — the per-frame
  sector-state slot is part of the frame record, and the render worker consumes the frozen slot
  contents. This section owns the ring's backend storage mechanics (Vulkan mapped memory / GL
  persistent-mapped) and the per-slot completion fence.
- **Section 5 (ticket 06, portals):** this section implements the backend pieces section 5 deferred —
  the per-portal occlusion-query pool and the dynamic window-poly buffer (in-order, fence-gated under
  the worker) — and streams the per-frame view state (per-portal composed transform, `frontFace`,
  scissor/stencil) that its step-5 draws consume; a draw is one (region, texture, bandIndex)
  sub-range.
- **Sections 3/6 (tickets 04/07, geometry/lighting):** the sub-range table (region, texture,
  bandIndex) + AABBs is the draw source; band sub-ranges + `uBandIndex`, the per-view uniform list,
  levelSkyPos, and the glow table are consumed by the walk/VS above; the fragment adaptation block
  (#define promoted uniforms → varyings) is the levelmesh shader variant this section wires up.
- **Section 8 (ticket 11, game-logic feedback):** the per-view sprite gather's visible-sprite list is
  its input (the RenderedTargets replacement for the dither-transparency loop); its dither tail runs
  inside the frame build (amendments A1–A8 below).
- **Sections 9/10 (A/B, baseline/perf):** A/B at zero tolerance (section 9 drives `gl_uselevelmesh`);
  slaughter-scale perf items: per-sub-range cull walk, per-frame sector-state upload (≈4.8 MB at 50k
  sectors), query issue + result read, per-view sprite gather, worker overhead (fence waits, submit).
  The **`gl_uselevelmesh` default-flip decision is 09/10's**.
- **Glossary (section 2):** new terms — "Render worker (levelmesh)", "Handoff slot", "Window polygon"
  (following the 05/06/07 pattern).

### Amendments (post-resolution, ticket 11 — 2026-10-04)

- **A1 — Suplex-pitch-window cull for OOB non-radar views.** OOB views without `r_radarclipper` (or
  `LEVEL3_NOFOGOFWAR`) must cull with the classic's expanded vertical window — pitch ± (20 + ½FOV),
  capped at 179° (classic `hw_drawinfo.cpp:490-496`) — not the exact frustum. Their culled lists are
  the exposure candidate sets (section 8), so the expanded bounds apply to the list itself, not a side
  pass.
- **A2 — OOB radar views: per-subsector radar gate in the cull walk.** When `r_radarclipper &&
  !LEVEL3_NOFOGOFWAR && bDoOob`, each candidate subsector fails the walk unless its radar gate passes
  (classic `hw_bsp.cpp:800-868`): already-exposed subsectors pass in non-deathmatch; otherwise the
  subsector is radar-ray-tested (section 8's BSP raycast, radar solidness, origin = `Viewpoint.OffPos`).
  A failing subsector is **not drawn** (radar-gated rendering, not just exposure). Deathmatch: every
  subsector re-tested per frame.
- **A3 — Ortho no-fog views: viewbox cull.** `bDoOrtho && (!r_radarclipper || LEVEL3_NOFOGOFWAR)`: the
  walk tests subsector bboxes against the 2D viewbox (`ext = 3 · offset · tan(½FOV)`, classic
  `RenderOrthoNoFog`, `hw_bsp.cpp:1057-1075`) instead of the frustum.
- **A4 — Secret render gate.** OOB views without `r_radarclipper` exclude undiscovered-secret sectors'
  subsectors from the culled list (`hw_bsp.cpp:797`) and things inside them from the visible-sprite
  list (`hw_bsp.cpp:637`). Normal views are un-gated (the automap's secret display gating is shared
  `am_` code).
- **A5 — Static line→sub-range table.** At build, per line (and per flat plane / 3D-floor plane): the
  sub-range(s) containing it, the index range per sub-range, and the per-line **segCount within each
  sub-range** (the dither `dithertranscount` decrement unit, section 8). Plus per-seg `map-exempt` bit
  (same-sector 2s line with an invalid mid texture) and per-subsector `hasSidedefSegs` bit (build-time;
  together the table feeds the dither fragments, the `dithertranscount` decrement, and the exposure
  nuance — ticket 11).
- **A6 — `DITHERTRANS` pipeline-variant dimension.** The levelmesh pipeline key set gains the dither
  bit: dither fragments (section 8) draw the same sub-range index range with the `EFF_DITHERTRANS` /
  `main.fp #define DITHERTRANS` shader (adapted per 07's promoted-uniform variant).
- **A7 — Dither target source.** The per-view visible-sprite list is the dither target source: filter
  by `CurrentMapSections` + the A4 secret gate + eligibility `(MF3_ISMONSTER && !MF_CORPSE) ||
  MF_MISSILE` + per-frame dedup; sort by `thing->subsector` index (classic traversal order — exact in
  OOB mode, no pruning); cap 20.
- **A8 — Frame-build order per view** (game thread, interpolation window): sprite gather → **dither
  tail** (section 8: `P_CheckSight` + `SetDitherTransFlags`, OOB non-portal views) → cull walk (dither
  fragment splitting + exposure marking + `cullcolor`, section 8) → slot handoff. The tail precedes
  the walk (same-frame dither consumption, classic tail → RenderScene order) and is the last
  playsim-mutating step; the worker never touches `validcount`-keyed memos.

## 8. Per-frame game-logic feedback (ticket 11)

**What this section locks.** Ticket 11 (grilling, 2 rounds; resolved 2026-10-04, every branch
explicitly chosen/confirmed by the user) locks the two per-frame game-logic contracts that ticket 02's
consumer inventory proved the levelmesh path must replicate: the **exposure flags** (`SSECMF_DRAWN`
subsector mark, `ML_MAPPED` line mark — read by the automap, `am_map.cpp:2103,2240,2692,3074,3359`,
and serialized into savegames, `p_saveg.cpp:347-440`) and the **dither-transparency loop** (visible
actor → `P_CheckSight` → trace-based `WALLF_DITHERTRANS_*` writes consumed by the same frame's
drawing). Decisions: exposure = a per-frame per-view **candidate LOS pass** (D1, user-chosen over a
full occlusion pass replicating the classic clipper and over pure frustum piggyback); dither
transparency = a **full port now**, gated behind the off-by-default `r_dithertransparency` cvar (D2);
`Level->cullcolor` reproduced from the culled draw list (D3); test points = all fan vertices (D4);
ray mechanism = a new BSP raycast that walks the glnodes tree (D5); the `cullcolor` gate is
frustum-only, omitting the classic's occlusion gate — one documented delta (D6). The section also
locks the per-view frame-build order (section 7, amendment A8), the savegame round trip (unchanged
shared code), and the acceptance checklist that feeds section 9. "Exposure pass" and "Dither
fragment" are new glossary terms (section 2).

### Frame timeline (game thread, inside the interpolation window)

Per frame, views in classic view order (main → offscreen → portal views):

1. (section 4, once) Snapshot pass — sector-state ring slot.
2. (section 7) Sprite gather for the view → visible-sprite list.
3. (section 8) **Dither tail** (OOB non-portal views only): target collection + `P_CheckSight` +
   `SetDitherTransFlags` — writes flags for this frame.
4. (sections 7 + 8) **Cull walk** → culled draw list, during which: dither fragment splitting (reads
   step 3's flags + last frame's residue), exposure marking, `cullcolor` computation.
5. Slot handoff: the portal-view list (registration — sections 5/7) is filled with the previous
   frame's query gate flags; the worker then issues the 3D frame, including the occlusion-query
   prediction + issue for the next frame (section 7).

Ordering constraint: the dither tail precedes the cull walk (same-frame consumption, mirroring the
classic's tail → RenderScene order). The tail is the last playsim-mutating step of view build
(`P_CheckSight`/`Trace` touch `validcount`-keyed memos); nothing in the view after it — and nothing
in the worker — may depend on those memos. This is the per-view frame-build order locked as section
7's amendment A8: sprite gather → dither tail → cull walk (dither splitting + exposure +
`cullcolor`) → slot handoff.

### Exposure pass (per view)

**Candidate sets** (per view mode):

| View mode | Candidates |
|---|---|
| Normal (`!bDoOob`) | subsectors in the view's culled draw list (exact frustum) |
| OOB, non-radar (`!r_radarclipper \|\| LEVEL3_NOFOGOFWAR`) | subsectors in the culled list computed with the **suplex pitch window** (section 7, amendment A1: expanded vertical bounds) |
| OOB, radar (`r_radarclipper && !LEVEL3_NOFOGOFWAR && bDoOob`) | subsectors in the culled list; the radar gate also culls (section 7, amendment A2) |
| Ortho no-fog | subsectors in the viewbox-culled list (section 7, amendment A3) |

**Per candidate subsector S** (only while `!(S->flags & SSECMF_DRAWN)`):

1. OOB secret gate: `bDoOob && !r_radarclipper && S->sector` is an undiscovered secret
   (`isSecret() && wasSecret()`) → skip (no draw, no exposure — section 7, amendment A4).
2. Mode test:
   - OOB non-radar: mark (piggyback) — except when S has **no seg with a sidedef** (precomputed
     `hasSidedefSegs` bit — the classic's 1s-only-subsector nuance).
   - OOB radar: radar gate = radar ray test over all fan vertices (radar solidness, origin =
     `Viewpoint.OffPos`). Pass → mark. Fail → S is also removed from this frame's draw list (radar-
     gated rendering). Deathmatch: no already-exposed fast path (every candidate tested per frame).
   - Normal: standard ray test over all fan vertices (standard solidness, origin = `Viewpoint.Pos`).
3. On mark: `S->flags |= SSECMF_DRAWN`; for each seg of S with a linedef that is **not map-exempt**
   (precomputed per seg: same-sector 2s line with an invalid mid texture):
   `linedef->flags |= ML_MAPPED`. Marks are monotonic — once exposed, never re-tested, never cleared.
   Re-testing of still-occluded candidates runs every frame (deterministic, frame-rate independent
   for a given camera path — the classic re-tests per frame too).

**Test points (D4):** all distinct vertices of the subsector's seg fan (typically 3–6), precomputed
at build. This gives the best coverage of the fan's angular extent — the classic marks when the
subsector's full angular range is not covered by a *single* occluder; more points approximate that
rule more closely. Reusing the fan vertices is also what the levelmesh's static data already carries,
so no extra structure is needed.

**BSP raycast (new levelmesh utility, D5):**

- Input: 2D origin, 2D test point, solidness mode (standard/radar). Walk the glnodes tree: at each
  node, determine which side the segment lies on / whether it crosses the node's split line; on a
  segment-vs-node-seg intersection, test the solidness predicate; solid → return BLOCKED, non-solid →
  continue the walk (the ray passes through, exactly as non-solid lines add no clip range in the
  classic). Track the sector on the origin side of each hit for the 2s fake-flat evaluation.
- **Solidness predicates** (classic sources, evaluated at *current* plane heights):
  - *Standard*: 1s or `WALLF_BLOCKRENDERING` → solid, unless `WALLF_DITHERTRANS_MID`; 2s: polyobject
    → never; same sector → map-exempt if the mid texture is invalid (ray passes); different sector →
    solid iff `hw_CheckClip(sidedef, currentsector, hw_FakeFlat(backsector, ...))`, unless
    `WALLF_DITHERTRANS_MID`.
  - *Radar*: 1s → always solid (no height check, no dither exclusion); 2s: polyobject → never; else
    solid iff `hw_CheckClip(...)` (the classic's radar padding angle is a range-coverage detail
    irrelevant to a ray test — documented, negligible).
- Reuses the existing `hw_CheckClip`/`hw_FakeFlat` height evaluators directly. Deterministic: static
  tree and seg order; the only per-frame inputs are the origin and the predicate heights. O(tree
  depth + hits) — chosen over reusing the classic line-walk trace (unbounded cost on dense line
  layouts).

**`cullcolor` (D3, per view, in view order):** for each seg with a sidedef in the culled list:
`r_distance_cull_type > 0 && IsDistanceCulled(seg) && frontsector->Colormap.FadeColor != 0` →
`Level->cullcolor = FadeColor`. Latching unchanged (no per-frame reset; `gl_cullcolor` initialization
unchanged). Per D6 the classic's occlusion gate is omitted (documented delta — a culled line behind
a wall can set the color; the value latches per level, so the effect is transient). Consumers are
section 6's: the view clear color and the fog color when `r_distance_cull_type > 1`.

### Dither loop (per OOB non-portal view)

Gate: `r_dithertransparency && doOob && RTnum < MAXDITHERACTORS && mCurrentPortal == nullptr` — any
OOB non-portal view (main or offscreen) runs its own loop; `MAXDITHERACTORS = 20`.

1. **Target collection** from the view's visible-sprite list (section 7): filter by
   `CurrentMapSections[thing->subsector->mapsection]`, the OOB secret gate (OOB `!r_radarclipper`:
   things in undiscovered secrets excluded — classic `hw_bsp.cpp:637`), eligibility
   `(MF3_ISMONSTER && !MF_CORPSE) || MF_MISSILE`, and per-frame dedup (`thing->validcount`); sort by
   `thing->subsector` index ascending (the classic's traversal order — exact, because the OOB
   traversal has no pruning); cap at 20. (Section 7, amendment A7.)
2. **Per target**: `P_CheckSight(players[consoleplayer].mo, target, 0)` (keep the player-pawn
   asymmetry — sight from the **console player's pawn**, not the camera) → `SetDitherTransFlags(target)`
   — the classic method verbatim (`hw_drawinfo.cpp:815`): 6 traces, `TRACE_PortalRestrict`,
   mapsection gate, tier/count rules, own-sector 3D-floor flags, ortho variant.
3. **Flag storage**: the classic's playsim storage — `FLineSide::Flags`
   (`WALLF_DITHERTRANS_TOP/MID/BOTTOM`) + `FLineSide::dithertranscount`, `plane_t::dithertransflag`,
   `F3DFloor` plane flags. No new GPU state; the dither effect is a shader variant, not data.
4. **Consumption** (during the cull walk, per view): for each surface (wall tier / flat plane /
   3D-floor plane) whose flag is set and whose sub-range is in this frame's list, append a **dither
   fragment** — the same sub-range index range drawn with the `DITHERTRANS` pipeline variant
   (section 7, amendment A6; the existing `EFF_DITHERTRANS` / `main.fp #define DITHERTRANS` shader
   adapted per section 6) — then consume it: TOP/BOTTOM flags cleared; MID: `dithertranscount -=
   segcount(line, sub-range)` (precomputed, section 7, amendment A5 — replicates the classic's
   per-seg decrement), flag cleared at 0; flat/3D-floor: `dithertransflag` cleared. Surfaces not in
   this frame's list keep their flags (residue — the classic's clear-on-consume semantics). Same-
   frame consumption holds: flags written by step 3 of view build are consumed by step 4 of the same
   frame, exactly as the classic's tail → RenderScene order.

### Savegame round-trip

No change: the flags live on `subsector_t`/`line_t`, the levelmesh writes the same fields, and
`p_saveg.cpp:347-440` (incl. the `SSECMF_DRAWN`-from-`ML_MAPPED` recovery path) is shared code.

**A/B tool note (for section 9):** exposure and dither state are *monotonic game state*. An A/B
comparison of frames exhibiting these effects must **reload the level per path** (or reset the
flags) — alternating paths on a live level cross-contaminates the flags.

### Acceptance checks (feed section 9)

1. **Exposure parity (default cvars):** scripted camera walk; compare per-subsector `SSECMF_DRAWN` +
   per-line `ML_MAPPED` between classic and levelmesh at matched tick/camera state. Target:
   identical sets. Any diff must be classified as an approved candidate-approximation delta (a
   subsector whose full range is covered by no single occluder, but whose fan vertices are each
   blocked by different lines) or a bug. Strong proxy: savegame byte-diff of the flag sections after
   the same scripted walk.
2. **Automap visual:** A/B automap screenshot after the same walk (default cvars) — zero tolerance
   (the automap is the shared 2D pass; its only input here is the flags).
3. **Secrets:** map with undiscovered secrets — OOB camera (`!r_radarclipper`): secret geometry
   neither rendered nor exposed; normal view: secret exposure un-gated (classic behavior).
4. **Dither:** `r_dithertransparency 1` + OOB camera actor + map with a monster behind partial walls
   — pixel A/B of the 3D frame (zero tolerance); decay test (actor leaves view → wall returns to
   normal within `dithertranscount` frames, frame-for-frame match); crowded-scene cap-20 test.
5. **`cullcolor`:** map with fade sectors + `r_distance_cull_type 2` — fog + clear color match
   (non-default cvar → behavioral check; the frustum-only-gate delta is documented and allowed).
6. **Save/restore:** mid-exploration save (partial fog + dither active) on the levelmesh path → exit
   → load → automap + dither state equal the classic's equivalent round trip.
7. **Radar mode** (`r_radarclipper`, dev cvar): OOB camera → radar-gated rendering + fog
   accumulation; documented delta allowed (full static occluder set vs the classic's exposure-
   dependent radar-clipper accumulation) — not zero tolerance.
8. **Ortho OOB** (`VPSF_ORTHOGRAPHIC` actor, no-fog): viewbox cull + exposure per section 7, amendment
   A3.

## 9. Verification & acceptance (ticket 09)

**What this section locks.** Ticket 09 (grilling, 3 rounds; resolved 2026-10-04, every branch
explicitly chosen by the user) locks the acceptance plan: the deterministic A/B rendering tooling
(an always-compiled engine capture mode + an external driver), the acceptance matrix (amended
2026-10-04/05), the zero-tolerance / allowlist policy (ADR 0004), the perf protocol + bars, the
PROVEN gate that flips `gl_uselevelmesh` to default, and the human-driven soak checklist. One
distinction must be kept (section 11, "Distinction", states it in full): the perf kit at
`tools/abtest/` **EXISTS on disk today** — ticket 10 built and ran it (section 10) — while the
engine A/B capture mode and the external `tools/abtest.py` driver specified here are **BUILT by the
implementation effort**. They share the `tools/abtest/` home; they are different things.

### A/B capture mode (engine, to be built)

- **Always-compiled, cvar-gated, default off.** The cvar name is a spec detail
  (e.g. `r_ab_capture <ticks> <outdir>`).
- **Synthetic clock — determinism is built in-engine, not emulated from the driver.** Verified
  constraint (ticket 09): no existing path is tick-precise — `I_GetTimeFrac()` is wall-clock even
  under `-timedemo`/`cl_capfps` (d_main.cpp:585, hw_entrypoint.cpp:319, r_utility.cpp:974-975).
  When the mode is active: exactly one tick per rendered frame, real-time paced
  (`I_WaitForTic`-style), so the frame after tick N shows post-N state; the interpolation fraction
  is pinned at 0.5 (`DoInterpolations`), and sky scroll (the `levelSkyPos` inputs, section 6),
  texture animation (`TexAnim`), and camera FOV interpolation are computed from the tick time, never
  the wall clock.
- **Capture = the final composed framebuffer.** After final composition (post-2D — 3D world + HUD +
  overlays), the ticks in the list are written as PNGs named `<map>_<tick>.png` to the output dir,
  reusing the existing `M_ScreenShot` PNG writer (g_game.cpp). The HUD is identical code on both
  paths — free parity coverage at one capture point.
- **Demos.** The mode also accepts `-playdemo` input; demo input is consumed per tick in the sim,
  so replay is identical under synthetic pacing for both paths. The demos are section 10's
  synthetic set (one per map).

### A/B driver, manifest, run structure (external, to be built)

- `tools/abtest.py` (Python 3, Pillow + stdlib) + a committed manifest (maps, WADs, tick lists,
  mode, resolution) + committed allowlists under `tools/abtest/allowlists/`.
- **Run structure.** Per map × mode × backend: **two engine launches** (classic, levelmesh), each
  a **fresh level load** — exposure and dither state are monotonic per load (section 8, "A/B tool
  note"), so a single session cannot host both paths. **Same-backend pairs only** (D8): parity is
  per-backend — classic-GL33 vs levelmesh-GL33, classic-Vulkan vs levelmesh-Vulkan — and **both
  backends must pass**. Config is applied per run via a temp autoexec: the resolution,
  `vid_preferbackend`, `gl_uselevelmesh` (section 7's selection cvar), gamma 1.0, vsync off,
  `vid_maxfps 0`, `cl_capfps` off, default visual cvars, fixed language/UI scale, screenshot
  notification silenced.
- **Modes.** *Static tick lists* — no input; the per-map tick list must cover the first ~200 tics
  (spawn state, thinker init), a tick at a light-thinker/animated-flat frame boundary, and a tick
  with a door open or a lift mid-transit; concrete ticks per map are pinned in the manifest at
  first bring-up. *Demo replay* — the map's demo from section 10's set replayed on both paths,
  captured at fixed ticks (e.g. every 30 tics).
- **Resolution: host-native 2560×1440, pinned** (amended 2026-10-05, user decision: the resolution
  axis is dropped — timedemo is CPU-bound at that scene size, so the axis measured nothing;
  calibration evidence in section 10).
- **Cvar sweep.** Ticket 07's sweep (section 6, "Parity checklist": r_lightmode 0/1/2/8,
  gl_fogmode 0/1/2, r_fakecontrast 0/1/2, r_extralight ±, r_distance_cull_type 0/1/2,
  gl_weaponlight 0/1, lightmaps on) on the fog / fake-contrast / animated-light / scroll / skybox
  maps already in
  the matrix, plus gamma {1.0, 0.9} × glow on/off on the stress maps.
- **Comparator.** Per-pixel diff of the PNG pair; connected components of the diff bitmap are the
  clusters; any pixel outside the allowlist fails the run.
- **Where it runs.** On a GPU dev machine with a display; **not a CI job** — no headless mode
  exists (GL33 needs a display context, Vulkan a GPU; CI runners are build-only).

### Acceptance matrix (amended 2026-10-04/05)

WAD set user-confirmed on hand 2026-10-04; the classic-ports row (TNT/Ritual/Rampage/Spectre) and
`doom1.wad` were dropped the same day (user decision) — the acceptance set is the WADs actually
present in `tools/abtest/wads/`.

| WAD | maps | what they pin |
|---|---|---|
| DOOM2.WAD | MAP01, MAP07, MAP21, MAP23, MAP27, MAP30, MAP31 | baseline; 3D floors + lift (MAP07/21/30); killfloor scale (MAP21); light thinkers (MAP23); fog (MAP27); dense scale (MAP31) |
| HEXEN.WAD | MAP01, MAP10, MAP27 | vanilla BEHA-format coverage (pinned 2026-10-04, phase 1): teleport lines 80, lifts, sector special bits; **no category pins** — scanner-verified zero line portals / sector_link / 3D floors / skybox / fog / fake contrast; vanilla Hexen's 170 polyobject lines (special 1 = Polyobj_StartLine, playsim/actionspecials.h:25, across all 31 maps) are format coverage, not a category pin |
| HERETIC.WAD | E1M2, E5M6 | vanilla DOOM-format coverage (pinned 2026-10-04, phase 1): door/lift density, sector specials; **no feature pins** — scanner-verified |
| myhouse.pk3 | MAP01, 20PAM | at scale (MAP01: 165,922 lines / 35,853 sectors): portals (MAP01: 508, incl. 98× Sector_SetPortal; 20PAM: 60× Line_SetPortal), sector_link (20PAM, 7× special 107), skybox (MAP01: 5× TID-less SkyViewpoint + 50 SkyPickers), fake contrast (MAP01: 4804 nofakecontrast sides), 3D floors (1065), polyobjects (1693) — four of the original five category locks in one archive |
| Pirates!.wad | MAP43, MAP49, MAP50, MAP51, MAP54, MAP57, MAP58 | fog (MAP50/51/57, `fogdensity`) — the fifth category lock; polyobjects + 3D floors (MAP43/49/54/58) |
| SOS_Boom.wad | MAP12, MAP32, MAP45, MAP46 | slaughter scale: MAP32 = 61,623 lines / 9,907 sectors / 76,458 verts (the scale-payoff bar below applies to MAP32); MAP12/45/46 secondary scale; no feature pins (scanner-verified; UMAPINFO carries only sky textures, music, map flow) |
| planisf2.wad | MAP01 | scale + geometry coverage: 37,265 lines / 6,287 sectors; no feature pins |

**Run axes.** Every map above × {static tick lists, demo replay} × {GL33, Vulkan} — 26 maps →
104 map×mode×backend combinations, two fresh-level-load launches each (run structure above),
**both backends must pass** (D8) — plus the cvar sweep. The per-WAD feature inventory behind the
pins is ticket 09's "WAD inventory" (scanner-verified 2026-10-04) and `tools/abtest/manifest.md`.

### Tolerance & known-diff allowlist (ADR 0004)

- **Zero tolerance outside a frozen per-map known-diff allowlist** (D3). The only legitimate source
  of a diff is z-fighting — a structural consequence of ticket 04 (the classic draws into a
  normalized per-view depth range, the levelmesh into true z; either winner is legitimate).
- **First bring-up:** the tool records every diff cluster per map+mode; a human reviews each —
  real bug → fix it; z-fight → allowlist entry. The allowlist is **committed JSON** (map, mode,
  tick, cluster rect/centroid + radius, reason) under `tools/abtest/allowlists/`, and **frozen**:
  any NEW diff fails; entries can only be **removed** (re-review), never added silently (ADR
  0004). (Ticket 09's original schema also listed "resolution" among the fields; that axis was
  dropped 2026-10-05.)
- Ticket 04's z-fighting budget is thus enforced exactly as allowed — nothing else.

### Profiling: protocol & bar

- **Metrics (D9).** Median + p95 of **three** metrics: total frame time, CPU render window, GPU
  3D window.
- **Measurement.** A per-frame perf log: the `r_perflog` cvar ticket 10 already landed in the
  engine (section 10's kit — it exists today) appends one line per frame — total frame (the
  existing `FrameCycles` timing in `D_Display`), CPU render window (`r` = the classic `RenderView`
  window — traversal + batch + issue), GPU 3D window (`fin` = finish/present — the classic fence
  wait). The levelmesh-side windows log the same way — the levelmesh frame-build window (cull walk
  + uploads) for the CPU render window, the render worker's fence wait (ticket 08) for the GPU 3D
  window — that extension is part of the implementation effort. Parsing: `parse_perflog.py`.
- **Protocol.** Per map + backend: 2560×1440 host-native, vsync off, gamma 1.0, default visual
  cvars, `-nomonsters`; 10 s warmup discarded, 60 s demo-driven window (the same demos as the A/B
  matrix — section 10's set); the reference machine is recorded in section 10 (the bar is
  relative — same machine, both paths).
- **Bar (D10).** (1) **No-regression floor:** levelmesh median total frame time ≤ classic × 1.05
  on every acceptance map, **both backends**. (2) **Scale payoff:** SOS_Boom (MAP32): levelmesh ≤
  50% of the classic median frame time. The classic medians are section 10's table — ticket 10 ran
  this exact protocol; its deliverable is the baseline levelmesh is compared against.
- **Buffer-streaming budget (formula-bound).** The per-frame levelmesh upload is
  `sectorCount × 96 B` (sector-state ring, ticket 05) + the per-level 3D light state
  (12 B/light, ticket 07) + the `levelSkyPos` triple; vertex uploads settle once per level
  (ticket 04 — height sub-uploads only while planes are unstable). At 50k sectors that is
  ≈ 4.8 MB/frame (≈ 288 MB/s H2D at 60 fps) — inside any realistic budget; ticket 10 measures the
  actual numbers on the real WADs.

### PROVEN gate

**PROVEN = A/B pass ∧ perf pass ∧ soak pass ∧ no open levelmesh-only bug of severity ≥ S2**
(D11), where:

- **Severities:** S1 = crash/hang/memory growth; S2 = visible artifact or broken behavior on an
  acceptance map under normal play; S3 = cosmetic/rare.
- **A/B pass** = every map × mode × backend in the matrix (both backends), zero tolerance outside
  the frozen allowlist, plus the cvar sweep; the pixel-A/B items of section 8's game-logic
  feedback checks (dither frames, automap screenshot) run through the same comparator under the
  reload-per-path run rule.
- **Perf pass** = the bar above. **Soak pass** = the checklist below.
- **Roles.** The implementer runs `tools/abtest.py` (full matrix) + the perf protocol and posts
  the results; the **map owner declares PROVEN**; the `gl_uselevelmesh` default flip (false at
  handoff — sections 1 and 7) and the SettingsPage entry (ticket 08) ship with that declaration.

### Soak checklist (human-driven, levelmesh on, ~half a day)

1. Full DOOM II campaign, Normal.
2. Full Hexen campaign (vanilla BEHA coverage; the feature categories are carried by the myhouse
   + Pirates! maps, items 4–5).
3. Heretic E1.
4. myhouse MAP01 + 20PAM tours, 10+ min each (portals + 3D floors + sector_link + skybox).
5. SOS_Boom MAP32: 15-min fast-camera fly (scale + sector-state upload budget); portal load is
   carried by myhouse MAP01 in the matrix (SOS_Boom has no portals).
6. Scripted save/load + automap fog + secret-gating walk — ticket 11's acceptance items (flag
   array / savegame diff, automap screenshot A/B, secret gating, dither + decay + cap-20,
   cullcolor, save/restore round trip, radar/ortho behavior — section 8, "Acceptance checks").
7. 15-min deathmatch on a portal map (dither + OOB views + exposure under stress).
8. Light-thinker / fog / fake-contrast tour (myhouse MAP01 + DOOM2 MAP27 + an animated-light map),
   10 min.
9. Stability watch throughout: no crash, no hang, no GPU memory growth; includes one 1 h+ session.

## 10. Baseline & performance bars (ticket 10)

**What this section locks.** Ticket 10 (task; resolved 2026-10-06) ran the classic-path baseline
profile on section 9's acceptance maps and records the reference machine, the run protocol, the
full 26-map results table (**52/52 PASS**), the observations that anchor section 9's performance
bars, and the L-line pin cross-check. The perf kit used here **EXISTS on disk today** under
`tools/abtest/` — `README.md` (kit documentation + protocol), `manifest.md` (the pins, the demo
set, the reference machine), `results/baseline.md` + `results/baseline.json` (this table plus the
full p95/per-field breakdowns) — cross-referenced in section 11, "Baseline perf kit". It is not
the section 9 A/B capture mode + `tools/abtest.py` driver, which the implementation effort builds
(section 11, "Distinction").

### Reference machine (dev box = reference)

Recorded 2026-10-04 — the bring-up ran on the dev box; it IS the reference machine:

- **OS:** Void Linux, kernel 7.2.9_1, Wayland session (display available in-session).
- **CPU:** AMD Ryzen 9 5900X, 12C/24T, max ~4.95 GHz.
- **GPU:** AMD Navi 31 (PCI 1002:744c, RX 7900 class), amdgpu kernel driver, Mesa userspace;
  real-GPU drivers verified per run — GL: `radeonsi, navi31, ACO`; Vulkan: `RADV NAVI31`
  (discrete).
- **RAM:** 31 GiB.
- **Build:** RelWithDebInfo (this tree).
- **Framebuffer:** pinned host-native **2560×1440** (the DP-1 panel; a 1920×1080 panel is also
  connected — virtual desktop 4480×1440), vsync off.

### Run protocol

- **Matrix:** 26 acceptance maps (section 9) × {GL33, Vulkan} = **52 runs**, each a fresh level
  load, driven by `tools/abtest/run_matrix.sh` (sequential; writes `logs/status.tsv`; per-run
  stdout checks).
- **Demos:** the 26 **synthetic** demos in `tools/abtest/demos/` — `make_demo.py` computes a
  seeded travel-to-spiral camera from each map's real geometry + the engine's exact tic model and
  packs a valid UZDoom `.lmp` (fixed seed, `demo_version 114`, `demo_compression`,
  `-nomonsters`, noclip2, 2600 tics). Deterministic — replays frame-identically across code
  changes except where a map's own scripted events diverge the sim (detectable from the perflog
  `L` line). This **replaced** ticket 09's original "record once with the classic path" step
  (ticket 10, 2026-10-05). All 26 replay-verified 2026-10-05 (full 2600 tics, exit 255, `L` lines
  match the pins).
- **Settings:** 2560×1440 via the `-width`/`-height` command-line FARGs (`vid_width`/`vid_height`
  are **not** cvars in this engine), `-nomonsters` on every run (the demo set is built for it),
  `baseline.cfg` pins (gamma 1.0, default visual cvars, vsync off), `vid_preferbackend` set
  explicitly per run — 0 = GL 3.3, 1 = Vulkan (the engine auto-selects a backend at startup; there
  is no `gl_backend` cvar).
- **Resolution axis dropped (2026-10-05, user decision):** a calibration run on DOOM2_MAP01
  (GL33, 2600 tics) showed per-frame cost identical at 320×200 / 1920×1080 / 3440×1440 (med
  ≈ 0.235 ms, p95 ≈ 0.26 ms, max ≈ 6.6 ms — a 2.4× pixel difference with zero cost difference):
  timedemo is CPU-bound (scene graph / draw-call side) at that scene size, so the axis measured
  nothing; every run pins the host-native panel instead.
- **PWAD mount:** the four non-commercial WADs (myhouse, Pirates!, planisf2, SOS_Boom) mount on
  the commercial base — `-iwad wads/DOOM2.WAD -file wads/<pwad>` (launched as a bare
  `-iwad <pwad>` the engine aborts before the demo loads — exit 0, silent, no stdout error); the
  three commercial IWADs (DOOM2/HEXEN/HERETIC) run standalone. Encoded in `run_matrix.sh`'s
  `mount_for`.
- **Real GPU only:** a sandboxed shell blocks `/dev/dri`, and the engine then **silently** renders
  with llvmpipe (software GL; the Vulkan backend finds no ICD and falls back to GL) while still
  exiting 255 with a full perflog — such numbers would be software numbers, not GPU numbers.
  `run_matrix.sh` rejects any run whose stdout shows llvmpipe or a failed Vulkan init; the matrix
  ran unsandboxed on the real GPU.
- **Completion check:** timedemo exits **255** on success (an `I_FatalError`-style shutdown) — 255
  is not a failure. `F <frame>` numbers the *rendered* frame (uncapped: 40k–160k per run; each run
  takes ~80–90 s regardless of map size — the full matrix ≈ 75 minutes) and `tic=` carries the
  demo tick. A run is accepted only if the perflog exists and the last `F` line's `tic=` ≥ 2599
  (there is no "F 2600" summary line; `grep '^F 2600'` also matches `F 2600x` — a false-positive
  trap). Perflogs live in the workspace (`tools/abtest/logs/`): sandboxed shells block the
  engine's `/tmp` writes, and `r_perflog` then fails silently.
- **Stats:** median + p95 of total frame, CPU render window (`r` = RenderView), and GPU 3D window
  (`fin` = finish/present) via `parse_perflog.py --warmup 350 --window 2100` — 350 tics (10 s)
  warmup discarded, 2100 tics (60 s) window.

### Results — 52/52 PASS

Classic path, reference machine, settings as above. Medians in ms per frame; render-CPU = `r`
(RenderView), frame = `total` (total frame), 3d-fin = `fin` (finish/present). p95 + full
per-field breakdowns: `tools/abtest/results/baseline.json`; prose + full table:
`tools/abtest/results/baseline.md`.

| WAD / map (sectors) | gl33 render-CPU | gl33 frame | gl33 3d-fin | vulkan render-CPU | vulkan frame | vulkan 3d-fin |
|---|---|---|---|---|---|---|
| DOOM2_MAP01 (59) | 0.035 | 0.464 | 0.315 | 0.075 | 0.518 | 0.328 |
| DOOM2_MAP07 (29) | 0.034 | 0.379 | 0.242 | 0.081 | 0.464 | 0.290 |
| DOOM2_MAP21 (61) | 0.025 | 0.324 | 0.198 | 0.052 | 0.394 | 0.235 |
| DOOM2_MAP23 (69) | 0.030 | 0.350 | 0.227 | 0.064 | 0.422 | 0.271 |
| DOOM2_MAP27 (187) | 0.031 | 0.378 | 0.241 | 0.068 | 0.443 | 0.282 |
| DOOM2_MAP30 (22) | 0.030 | 0.300 | 0.185 | 0.065 | 0.419 | 0.258 |
| DOOM2_MAP31 (80) | 0.029 | 0.339 | 0.200 | 0.060 | 0.401 | 0.231 |
| HERETIC_E1M2 (247) | 0.031 | 0.361 | 0.232 | 0.067 | 0.437 | 0.287 |
| HERETIC_E5M6 (416) | 0.045 | 0.506 | 0.340 | 0.102 | 0.611 | 0.404 |
| HEXEN_MAP01 (400) | 0.027 | 0.331 | 0.208 | 0.057 | 0.407 | 0.247 |
| HEXEN_MAP10 (337) | 0.030 | 0.385 | 0.251 | 0.066 | 0.481 | 0.309 |
| HEXEN_MAP27 (368) | 0.028 | 0.356 | 0.216 | 0.062 | 0.500 | 0.332 |
| MYHOUSE_20PAM (1075) | 0.069 | 0.528 | 0.312 | 0.115 | 0.628 | 0.341 |
| MYHOUSE_MAP01 (35853) | 0.203 | 0.821 | 0.346 | 0.326 | 0.988 | 0.395 |
| PIRATES_MAP43 (1092) | 0.050 | 0.510 | 0.308 | 0.076 | 0.539 | 0.338 |
| PIRATES_MAP49 (987) | 0.129 | 0.658 | 0.295 | 0.061 | 0.559 | 0.334 |
| PIRATES_MAP50 (3897) | 0.071 | 0.537 | 0.318 | 0.156 | 0.641 | 0.357 |
| PIRATES_MAP51 (536) | 0.028 | 0.340 | 0.202 | 0.056 | 0.391 | 0.232 |
| PIRATES_MAP54 (839) | 0.031 | 0.493 | 0.351 | 0.059 | 0.534 | 0.371 |
| PIRATES_MAP57 (1336) | 0.032 | 0.492 | 0.300 | 0.066 | 0.545 | 0.338 |
| PIRATES_MAP58 (1796) | 0.027 | 0.459 | 0.319 | 0.057 | 0.509 | 0.342 |
| PLANISF_MAP01 (6287) | 0.057 | 0.572 | 0.344 | 0.120 | 0.658 | 0.393 |
| SOS_MAP12 (2422) | 0.028 | 0.455 | 0.311 | 0.050 | 0.506 | 0.342 |
| SOS_MAP32 (9907) | 0.027 | 0.468 | 0.286 | 0.047 | 0.527 | 0.320 |
| SOS_MAP45 (1939) | 0.040 | 0.502 | 0.328 | 0.091 | 0.581 | 0.383 |
| SOS_MAP46 (2366) | 0.026 | 0.487 | 0.348 | 0.051 | 0.518 | 0.349 |

### Observations (anchoring section 9's bars)

- **Vulkan's CPU RenderView window ≈ 2× GL33's on every map** (0.05–0.33 ms vs 0.025–0.20 ms)
  while the GPU 3d-fin window is close (within ~0.1 ms) — the Vulkan gap is on the **CPU
  submission side** at these scene sizes, not the GPU.
- **Total frame time is flat 0.3–0.6 ms across most maps.** The two heaviest renders:
  **myhouse MAP01** 0.82/0.99 ms (35,853 sectors, portal map) and **PLANISF** 0.57/0.66 ms
  (37,265 lines; p95 spikes to 6.1/7.1 ms).
- These classic medians are what section 9's bars are measured against: **levelmesh ≤ classic ×
  1.05 per map, both backends; SOS_Boom ≤ 50% of its classic median**.

### L-line pin cross-check (all confirmed)

Each perflog's `L` line (per-level summary: map, sectors, lines, subsectors, sprites, polyobjs,
line portals, portal groups, 3D floors) was cross-checked against the `manifest.md` pins and the
static scanner; a mismatch blocks the results:

- **DOOM2 / HERETIC / SOS_Boom / planisf2:** no features, as pinned.
- **HEXEN:** polyobjs 12/7/9 active at load (vanilla format coverage).
- **myhouse 20PAM:** line portals = 60 (exact pin match).
- **myhouse MAP01:** 410 line portals (exact) + 89 portal groups vs the 98 static
  `Sector_SetPortal` — **delta 9, flagged** — + 9,812 3D floors + 169 polyobjs, 165,922 lines.
- **planisf2:** 37,265 lines / 6,287 sectors (exact).
- **Pirates!:** 3D floors on all 7 maps (160–1759), portal groups on MAP50 (6) / MAP58 (3),
  **polyobjs = 0 at load on all 7** — the polyobject pin is format coverage for these maps: start
  lines exist but nothing is active at level start (triggers need actors, `-nomonsters` is on, and
  the synthetic camera need not cross them); active-polyobj render coverage comes from HEXEN
  (12/7/9) + myhouse MAP01 (169) instead.

## 11. References

**ADRs** (`docs/adr/`):

- `0001-levelmesh-full-parity.md` — the levelmesh path holds full parity with the classic path,
  including `PORTAL` line specials and `sector_link`; "pragmatic parity" was rejected, which makes
  portal rendering a first-class design problem and the acceptance bar correspondingly higher.
- `0002-dynamic-geometry-vertex-warp.md` — moving and animated sector planes are not re-uploaded
  as vertices: the vertex shader warps the static mesh from a per-frame sector state buffer whose
  records carry the single current, CPU-interpolated plane values (no dynamic VBO, no GPU-side
  tick interpolation), making A/B parity on moving geometry exact by construction.
- `0003-full-frame-sector-state-streaming.md` — the snapshot pass copies every sector state record
  into the current slot of a ring of `HW_MAX_PIPELINE_BUFFERS` slots every frame, with no dirty
  tracking; the buffer is small, and byte-identical-per-run keeps the A/B acceptance tool
  deterministic and trivially diffable.
- `0004-ab-acceptance-policy.md` — A/B acceptance is zero-tolerance pixel diffing with exactly one
  structural exception: a frozen, human-reviewed, committed known-diff allowlist per map whose only
  legitimate source is z-fighting; any new diff fails acceptance, and entries can only be removed,
  never added silently.

**Research findings** (`.scratch/levelmesh-rendering/research/`):

- `01-helion-levelmesh.md` — deep dive into Helion's levelmesh pipeline: static per-texture VBOs
  baked at load, a per-plane dynamic bitmask driving a prev/current dynamic VBO that the GPU
  interpolates by tick fraction, a per-sector GPU light buffer, geometry-shader sprite billboards,
  and a flood-fill + fake-wall + stencil portal system — every claim cited to a Helion source file.
- `02-consumer-inventory.md` — a file:line inventory of everything that consumes the classic path's
  per-frame visibility results, proving the "render-only" assumption is broken in two places
  (per-frame automap/savegame flags and the dither-transparency feedback loop) — the basis for
  ticket 11.
- `03-portal-flow.md` — a file:line documentation of how the classic BSP/HW renderer draws through
  `PORTAL` line specials and sector_link/stacked-sector portal planes (portal data structures,
  per-frame registration, and draw flow), giving the levelmesh path a precise parity target.

**Baseline perf kit** (`tools/abtest/`):

- `README.md` — kit documentation: the `r_perflog` cvar, the `baseline.cfg` cvar pins, the WAD
  feature scanner, the synthetic demo generator, the perflog parser, the run-matrix driver, and the
  ticket-09 protocol.
- `manifest.md` — the pins: the WAD set, the locked per-slot map matrix, the demo set, and the
  recorded reference machine.
- `results/baseline.md` + `results/baseline.json` — the measured classic-path baseline (26
  acceptance maps × GL33/Vulkan, 52/52 PASS): median + p95 of total frame, CPU render window, and
  GPU 3D window at 2560×1440 host-native, anchoring the performance bars in section 10.

**Distinction.** The perf kit above EXISTS today — ticket 10 (classic-path baseline) built and ran
it, and the baseline results are on disk. The in-engine A/B capture mode (cvar-gated, synthetic
clock, per-tick final-composed-framebuffer PNGs) plus the external `tools/abtest.py` comparison
driver described in section 9 are BUILT by the implementation effort — do not conflate the two.

**Tickets.** The resolved tickets live in `.scratch/levelmesh-rendering/issues/` (tickets 01–11),
under the wayfinder map at `.scratch/levelmesh-rendering/map.md`; sections 3–10 of this spec map
1:1 to tickets 04–11 (§3↔04, §4↔05, §5↔06, §6↔07, §7↔08, §8↔11, §9↔09, §10↔10).
