# UZDoom Levelmesh Rendering

Domain glossary for the GPU-driven levelmesh effort. It defines the terms used by
`.scratch/levelmesh-impl/map.md` and `docs/adr/0001`–`0003`. Do not reuse these terms for unrelated
concepts.

## Language

**Levelmesh path**:
The Vulkan-only rasterization pipeline. The whole level is baked once at map load into clusters. Each
view is culled on the GPU (frustum + two-phase Hi-Z), which writes indirect draw commands; materials
are bound bindlessly. Per-frame CPU cost does not depend on level size. The GL backend never uses it.
_Avoid_: GPU renderer, new renderer, mesh renderer

**Classic path**:
The per-frame front-to-back BSP-traversal rasterizer (`hw_bsp.cpp` → `HWWall`/`HWFlat` → draw lists).
It is the only path on GL, the fallback on Vulkan devices without the required features, and the A/B
reference.
_Avoid_: legacy renderer, BSP renderer, old path

**DoomLevelMesh**:
The existing class (`src/rendering/hwrenderer/doom_levelmesh.*`) that builds triangle geometry for the
Vulkan raytracer (`VkRaytrace`). It is unrelated to the levelmesh path's bake.
_Avoid_: levelmesh (the path)

**Bake**:
The load-time construction of the levelmesh. It runs the classic emitters (`HWWall::Process` through
`HWWallDispatcher`'s mesh-helper mode, plus the classic static flat builder) and converts their output
into plane-anchored surfaces (ADR 0003). Bake outputs that depend on the view (portal walls,
translucent surfaces, missing-texture fills) go to side lists instead of the mesh.
_Avoid_: build, reimplementation

**Plane-anchored vertex**:
A vertex stored as `(x, y, zOffset, anchor)`. Its z is `ZatPoint(anchor plane) + zOffset`, evaluated
every frame from the sector state, so moving sectors need no vertex updates and there is no
static/dynamic lifecycle.
_Avoid_: dynamic vertex, baked z

**Plane ref**:
A `(sector index, floor|ceiling)` reference into the sector state buffer. It can point at a 3D-floor
model sector, which is how 3D-floor surfaces move with their model.
_Avoid_: plane pointer, height reference

**Surface record**:
The integer std430 record for each baked wall piece or flat. It holds plane refs, light ref,
side-state ref, untranslated texture id, draw-state bits (bucket, mask, clamp, cull), flags, baked
static light ingredients, lightmap page/UV, and normal.
_Avoid_: surface struct (too generic)

**Cluster**:
A spatial group of surfaces cut from a BSP subtree at a triangle budget. It carries an xy AABB, a
z-range that is refreshed when back-linked sectors move, and an index range. It is the unit of GPU
culling. Clusters are spatial only; they know nothing about portal topology.
_Avoid_: region (old design), chunk, meshlet (unless it really is one)

**Bucket**:
A draw-pipeline class of surfaces: opaque, masked, sky, portal window, or a material shader variant.
The cull writes indirect commands per bucket. A texture swap can move a surface to another bucket by
rewriting its bucket bits.
_Avoid_: texture group, sub-range

**Two-phase Hi-Z cull**:
The per-view GPU visibility scheme. Phase 1 draws the clusters that were visible last frame
(frustum-tested). A depth pyramid is then built from that depth. Phase 2 tests the remaining clusters
against the frustum and the pyramid and draws the ones that pass. Last, the view's persistent
visibility bitset is updated.
_Avoid_: occlusion query (that is only a fallback concept), PVS

**State buffers**:
Device-local SSBOs indexed by game objects: sector state (planes, light ingredients, colormap, glow),
side state (texture id, offsets, scales per tier), plane xform, polyobject transforms, 3D-light state,
and the texture translation table. Each one is updated only when its contents change, through a staging
ring. None of them is streamed in full every frame.
_Avoid_: sector ring, snapshot

**Texture translation table**:
The per-frame map from untranslated texture id to bindless slot. It follows frame animation, so
surface records keep untranslated ids and animation never touches geometry.
_Avoid_: animation table

**Shading resolve**:
The compute pass that runs once per visible surface per view. It evaluates the ported classic light
chain (effective light → rescale → rel light → lightmode → colormap, fog, desaturation, glow) into a
compact resolved record. The vertex stage reads that record through a `flat` surface index. Light is
never evaluated per vertex.
_Avoid_: light chain in VS, lighting pass (too generic)

**Light band**:
One draw of a wall inside a 3D-floor light volume, clipped to a single light layer by split-plane clip
distances. Bands are recorded at bake time; ticket 12 covers them.
_Avoid_: lightlist (that is the 3D-light state)

**Visibility feedback**:
The per-view cluster and surface visibility bits that the GPU cull produces. They are copied to a
host-readable ring and read by CPU consumers (actor/decal gather, exposure, dither, translucent set)
at most one pipeline depth later.
_Avoid_: readback (too generic), occlusion results

**Mark pass**:
The re-rasterization of a view's levelmesh surfaces against its final depth (EQUAL for drawn
surfaces, LEQUAL for sky surfaces and exposure quads) that sets a bit per leaf (subsector) and per
line with a visible surface. It is the visibility feedback the exposure marks (`SSECMF_DRAWN`,
`ML_MAPPED`) and the cullcolor latch come from (ticket 18).
_Avoid_: occlusion query, exposure pass

**Exposure quad**:
A bake-only wall quad (front floor to front ceiling along a whole side) of a two-sided line, or of a
one-sided line whose wall the levelmesh does not draw. Only the mark pass rasterizes it, so a line
seen through an opening is exposed like the classic's clipper-passing segs.
_Avoid_: portal quad, invisible wall

**Side list**:
A bake output for surfaces the opaque mesh cannot own: translucent and fog-boundary surfaces, portal
windows, and missing-texture/flood fills. The CPU-driven passes in tickets 15 and 16 draw them.
_Avoid_: leftovers, classic lists

**Transitional period**:
The span of tickets 08–19. During it, the classic BSP walk still runs on levelmesh levels and draws
whatever the levelmesh does not yet own. Ticket 20 removes the walk from the levelmesh frame. Perf bars
apply only after ticket 20.
_Avoid_: hybrid mode

**Delta class**:
A reviewed, documented category of acceptable A/B difference between classic and levelmesh (ADR 0002):
DC1 z-fight, DC2 float-z rounding, DC3 feedback latency, DC4 dynamic-light selection, DC5 visibility
precision (game-state sets). An unclassified diff fails. Adding a class requires amending ADR 0002.
_Avoid_: whitelist, tolerance budget

**A/B capture mode**:
The always-compiled, cvar-gated (default off) engine mode that makes rendering deterministic for the
A/B tool. It runs a synthetic clock (one tick per rendered frame, pinned interpolation fraction, sky,
texture animation, and FOV derived from tick time) and captures the final composed framebuffer as PNGs
per tick.
_Avoid_: screenshot mode, frame grab

**Stress vantage**:
A fixed camera scenario in the perf kit (ticket 02) placed on a large, open, portal-dense, or
3D-floor-dense view. It measures real rendering cost, unlike wandering-camera medians.
_Avoid_: benchmark, demo

**Proven**:
The state reached when A/B passes (acceptance matrix on Vulkan, delta-class policy), perf passes (the
bars in the map), soak passes, and no levelmesh-only bug of severity S2 or higher is open. The user
declares it. The `gl_uselevelmesh` default flip on Vulkan ships with that declaration.
_Avoid_: stable, done

**Soak**:
The human-driven in-game checklist run with the levelmesh on (ticket 21): campaigns, mod tours, a
save/load and automap walk, deathmatch, and a stability watch of 1 h or more.
_Avoid_: stress test, playtesting (too generic)
