# UZDoom Levelmesh Rendering

Domain glossary for the overhaul of UZDoom's level rasterization: replacing the per-frame BSP-traversal
rasterizer with a static GPU levelmesh (Helion-style). Terms are defined as they are locked by the
`.scratch/levelmesh-rendering/` effort; do not reuse them for unrelated concepts.

## Language

**Levelmesh path**:
The planned primary rasterization pipeline. The level is drawn as a static z-buffered mesh built once at
map load; per-frame CPU work is minimal, and visibility is resolved by the depth buffer.
_Avoid_: GPU renderer, new renderer, mesh renderer

**DoomLevelMesh**:
The existing class (`src/rendering/hwrenderer/doom_levelmesh.*`) that already builds whole-level geometry
at map load and is consumed only by the Vulkan raytracer. It is the seed of the levelmesh path, not the
levelmesh path itself.
_Avoid_: levelmesh (the path), static mesh

**Classic path**:
The legacy per-frame front-to-back BSP-traversal rasterizer (`hw_bsp.cpp` → drawsegs → batches). Retained
as a selectable fallback and the A/B reference until the levelmesh path is proven.
_Avoid_: legacy renderer, BSP renderer, old path

**Dynamic sector**:
A sector whose state (floor/ceiling heights or planes, light, textures) can change at runtime — doors,
lifts, 3D floor models. Topology is static in Doom: no runtime re-triangulation is ever needed.
_Avoid_: moving sector, animated sector, dynamic geometry

**Sector state buffer**:
The per-sector dynamic attribute buffer: tilted floor/ceiling planes, scroll offsets, **lighting
ingredients** (raw lightlevel, per-plane light offsets, the 9-byte `FColormap`), glow color/height, and
the transdoor flag — one 96-byte record per sector, full-copied to the GPU **every frame** into a ring of
`HW_MAX_PIPELINE_BUFFERS` slots; each record carries the single *current* CPU-interpolated plane values.
Evaluated in the vertex shader to warp dynamic surfaces **and** to shade every surface (07 locked
fetch-by-all).
_Avoid_: instance buffer, dynamic buffer (too generic)

**Snapshot pass**:
The per-frame CPU pass, run inside levelmesh frame setup after `DoInterpolations`, that packs every
sector's state record into the current ring slot and diffs each record against last frame to drive the
static↔dynamic lifecycle (first movement → flip dynamic; N=2 unchanged frames → settled → re-bake).
_Avoid_: dirty check, update pass (too generic)

**Dynamic surface**:
A surface whose plane is evaluated in the vertex shader from the sector state buffer (via its plane
refs) instead of drawing baked z. The complement is a *static surface*, which draws its baked z
untouched. Selection is the surface record's dynamic flag, driven by the snapshot pass.
_Avoid_: animated surface, moving surface

**Full parity**:
The levelmesh path reproduces every visual behavior of the classic path: portals/sector_link, fake flats,
sky portals, dynamic lights, and all sector effects. It is the acceptance bar, not a stretch goal.
_Avoid_: feature-complete, equivalent

**Proven**:
The state in which the levelmesh path has passed the acceptance bar (parity on the acceptance map list,
the frame-time bar, and the deterministic A/B tool). It is the gate for making levelmesh the default.
_Avoid_: stable, done

**Level vertex pool**:
The single whole-level 32-byte vertex store built once at map load, ordered per region then per piece
(subsector fan, line quad). Write-once except for re-bake patches; the first-movement flag flip rewrites
the in-vertex dual-purpose slot.
_Avoid_: VBO (backend-specific), mesh buffer

**Dual-purpose slot**:
The 8-byte `(slotA, slotB)` pair in every vertex: `(baked z, baked v)` for static surfaces,
`(vparam, 0)` for dynamic surfaces. The surface record's dynamic flag selects the interpretation.
_Avoid_: generic slot, variant data

**Surface record**:
The 64-byte CPU-writable SSBO record per wall quad / per subsector fan: mutable texture index and
dynamic flag; static plane refs, light ref, region index, opening/unpegged/window parameters; and the
per-wall shading statics (side tier + Light/tierLight, baked fake-contrast rel pair, side light flags,
lightmap num, 3D-light ref, 3D band range). The wall-shading/sky block (16 B) came with 07; the record
was 48 B under 04. Reached from vertices through a per-vertex uint32 surface index.
_Avoid_: surface struct (too generic), surface attributes

**Light chain**:
The classic per-surface CPU light computation (effective light → rescale → rel light → lightmode
transform → colormap → vColor, plus fog/desaturation/glow setup). The levelmesh runs it **in the
vertex shader** as a formula-for-formula bit-exact port (C++ stays the source of truth; int32 on
integer paths, C++ op order in float32 on float paths; fake-contrast `rel` baked at build — no GPU
atan); its per-draw results ride to the fragment as per-vertex varyings, so `main.fp` is reused
unchanged (promoted uniforms `#define`d to varyings).
_Avoid_: shading, lighting pass (too generic)

**3D light band**:
The sub-draw of a wall that lies inside a 3D-floor volume, clipped to one layer's z-band — the
levelmesh's expression of the classic's split-plane draws (hw_walls.cpp). Band records (per-level
static table: light ref + split top/bottom plane refs) give IBO sub-ranges a third dimension:
(region, texture, **band**). Band planes are read from sector records at draw time, so they track
moving 3D floors.
_Avoid_: light slice, lightlist (that is the per-level 3D-light state buffer)

**levelSkyPos**:
The per-level per-frame sky scroll triple (`hw_sky1pos`, `hw_sky2pos`, `hw_skymistpos`), the only
dynamic sky state. Sky surfaces carry a static `skyScrollKind` (which scroll drives them, incl.
doublesky's second layer); all other sky properties (texture, angle, MBF line-texture transfer) are
resolved at build — so scrolling skies write zero sector-record bytes per frame.
_Avoid_: sky offset (05's reserved record slot), sky portal (that is 06's portal-view mechanism)

**Plane ref**:
A `(sector index, floor|ceiling)` pointer into the sector state buffer. Includes refs to control /
3D-floor model sectors — that is how fake-flat surfaces move.
_Avoid_: plane pointer, height reference

**Re-bake**:
Restoring a dynamic surface's static vertices to the new resting height and clearing its dynamic flag
once movement completes. A surface is re-bake-eligible only when every plane it references has settled.
_Avoid_: static bake, geometry update

**Fake-flat surface**:
An extra static flat surface in the vertex pool referencing a 3D-floor model sector's (`heightsec`)
plane — the levelmesh's expression of `hw_FakeFlat`. No second pass, no runtime copies.
_Avoid_: fake flat (the classic `hw_FakeFlat` runtime mechanism), 3D-floor plane

**Main region**:
The level region containing the player's start. The only region the main view draws — portal regions are
reachable only through portals, so they are never directly visible in the main view.
_Avoid_: main level, base region

**Portal region**:
A self-contained vertex + index block drawn under a region view transform, front-face flag, and
scissor/stencil — the levelmesh's unit of portal rendering. The level partitions into regions by a global
flood-fill over the sector graph: adjacency = two sectors sharing a two-sided *non-portal* line (portal
lines are barriers; one-sided lines never connect); each connected component is a region. A portal's
target is the region behind its destination line (line portals) or its destination sector (sector_link).
04 locked the storage container; 06 locked the semantics.
_Avoid_: portal mesh, region chunk

**Portal view**:
A region-scoped view that draws exactly one portal region (its target) plus any nested sub-views, under the
portal's own view transform + view clip, stencil-masked. Not per-portal mesh baking — the target region's
per-texture IBO sub-ranges (04) are drawn under a per-portal transform. Sky/skybox are instances of the
same mechanism.
_Avoid_: portal mesh, secondary viewport

**Portal visibility query**:
The GPU occlusion query that decides whether a portal is drawn: a depth-only draw of the portal's window
polygon (under the main view) with a depth-pass test. Issued one frame ahead for the *predicted candidate
set* (portals inside the predicted frustum — N+1 camera = current + measured velocity × dt, generous
margin), so the result is one frame stale; a prediction miss is a bounded 1–2 frame delay. The result gates
both the portal draw and its depth clear (no spurious un-occlude).
_Avoid_: LOS ray, P_CheckSight (that is gameplay sight, not portal visibility)

**Render worker (levelmesh)**:
The async render thread the levelmesh path runs on (Vulkan only): it owns the 3D world pass only —
per-frame uploads, the full levelmesh 3D issue, and per-slot completion fences. The game thread keeps the
sim, the frame build (inside the interpolation window), the 2D stack, and presentation; the worker consumes
frozen handoff-slot contents and never touches playsim state. An explicit deviation from both Helion and
UZDoom's current synchronous behavior.
_Avoid_: render thread (the classic `r_thread.*` is the software renderer's span drawer — different thing),
async renderer

**Handoff slot**:
One entry of the 05 sector-state ring, extended to be the per-frame record the game thread builds and the
worker consumes: sector-state records, 3D-light state, levelSkyPos, per-viewpoint values, the portal-view
list, the culled draw list, sprite/model vertex slot references, and the query results to apply. In-order,
fence-gated, no frame skipping — the game thread 2D-composes per frame in order, so every built frame's 3D
pass must complete.
_Avoid_: frame buffer, command queue

**Window polygon**:
The per-portal 2D quad (portal-adjacent endpoints × current portal-adjacent sector heights) drawn
depth-only under the main view to feed the portal visibility query. A small dynamic buffer, one quad per
portal, updated per frame during the portal-transform step.
_Avoid_: portal frustum, view frustum

**Exposure pass**:
The per-frame per-view levelmesh pass that reproduces the classic's automap fog-of-war writes (per-
subsector `SSECMF_DRAWN`, per-line `ML_MAPPED`) and the `cullcolor` latch. Candidates = the subsectors in
the view's culled draw list; each not-yet-exposed candidate is tested with 2.5D occlusion rays from the
camera to its static fan-vertex test points (BSP raycast under the classic's solidness rules — standard or
radar mode); any unoccluded ray exposes the subsector. Marks are monotonic. View-mode rules replicate the
classic exactly: normal views test; OOB non-radar views mark from the suplex-pitch-window culled list;
OOB radar views (dev cvar `r_radarclipper`) radar-gate per subsector (the gate also culls the draw); ortho
no-fog views use the viewbox; undiscovered secret sectors are gated in OOB views for both draw and
exposure.
_Avoid_: fog-of-war pass, culling pass (that is 08's cull walk)

**Dither fragment**:
The per-frame draw-list entry that re-issues a normal sub-range with the `DITHERTRANS` shader variant —
the levelmesh's expression of the classic's dither-transparency feedback loop (visible actor →
`P_CheckSight` → `SetDitherTransFlags` traces → tier flags → dithered draws). Fragments are appended
during the cull walk, reading flags written by that frame's dither tail plus last frame's residue; flags
are cleared on consumption, with `dithertranscount` decremented by the line's seg count in the sub-range.
_Avoid_: dither pass, transparency variant
