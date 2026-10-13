# Prior art: VKDoom / GZDoom levelmesh, HWMeshHelper, GPU-driven renderers

Research for ticket 04. Question: what did earlier levelmesh work actually do, and what should
tickets 05–08 learn from it?

**Project rule (user decision, 2026-10-12): no VKDoom code or architecture.** This document is a
list of lessons and pitfalls, not a design source. Nothing in it is to be copied or ported from
VKDoom. Where VKDoom and this plan arrive at similar ideas independently, that is noted so the
overlap can be reviewed. See "ADR 0003 overlap" at the end.

## Sources

| Source | Revision | Notes |
|---|---|---|
| VKDoom | `nashmuhandes/VkDoom` @ `09634479a` (2025-11-21) | dpjudas deleted `dpjudas/VkDoom`. This mirror carries the full history (23,530 commits). dpjudas's last commit is `23a344b08` (2025-08-05), and all VKDoom citations are at or before it. The original issue tracker is gone, so "recorded problems" come only from commit messages and code comments. |
| GZDoom | `ZDoom/gzdoom` @ `c26ce2e6c` (2026-08-10) | |
| UZDoom | this tree | |
| Helion | `Helion-Engine/Helion` @ `61c045a` (2026-10-11) | Plus PR descriptions #1727, #1783, #1786 (GitHub API). |
| Ironwail (Quake) | `andrei-drexler/ironwail` @ `08d5781` (2026-09-10) | Searched as GPU-driven prior art. |

Paths are repo-relative. "Verified" means read in source. "Inferred" is marked.

---

## 1. VKDoom

### 1.1 Did it draw the world from the levelmesh?

**Yes, optionally, and never by default.** `CVAR(Bool, gl_levelmesh, false, 0)`; the archive flag is
commented out (`src/rendering/hwrenderer/scene/hw_drawinfo.cpp:78`). The history:

- Drawing started in `6dd72e543` (2023-10-14, "Begin code for drawing the level mesh").
- It was turned off five days later in `3cff0de27` ("Turn off gl_levelmesh") and stayed off until
  the repo was archived.
- The rest of the levelmesh drove the raytraced lightmapper (`vk_lightmapper.cpp`,
  `wadsrc/static/shaders/lightmap/*`), which was its main product.

The levelmesh is used for the outermost view only. `HWDrawInfo::RenderPVS` takes the levelmesh path
only when `gl_levelmesh && !outer` (`hw_drawinfo.cpp:457`). Portal and mirror views always use the
classic BSP path (`e491186c8`, "Don't use the level mesh recursively through portals for now").

### 1.2 Frame model: CPU visibility, CPU draw lists

Verified in `hw_drawinfo.cpp:447-535` and `hw_bsp.cpp:399, 899, 1058`:

1. **Visibility is CPU-side.** With `uselevelmesh`, the classic BSP walk still runs, clipper
   included. It no longer emits `HWWall`/`HWFlat`; it only records `SeenSides` / `SeenSectors`
   (`addddcd8e`, 2025-05-26, "Use clipper to find visible walls for hw_levelmesh"). A threaded
   variant, `HWVisibleSetThreads::FindPVS`, came later (`ead947b8b`, 2025-05-29, "Add BSP walker
   class that can be run on multiple threads").
2. **Draw lists are built on the CPU per frame.** `DoomLevelMesh::AddSectorsToDrawLists` and
   `AddSidesToDrawLists` append each seen surface's pre-baked index ranges to per-pipeline lists
   (`src/rendering/hwrenderer/doom_levelmesh.cpp:613-670`).
3. **One draw per range.** `VkRenderState::DrawLevelMeshList` binds each pipeline, then calls
   `drawIndexed(range.Count(), 1, range.Start, 0, 0)` once per range
   (`src/common/rendering/vulkan/vk_renderstate.cpp:1065-1081`).
4. **No GPU culling and no indirect draws in the world pass.** Indirect draws exist only in the
   lightmapper (`e6f0fec1b`, `1a7407944` "Always use indirect draw" touch only
   `vk_lightmapper.cpp` and lightmap shaders). An experimental depth prepass and tiled-light
   dispatch are commented out (`hw_drawinfo.cpp:853-859`).
5. **Portals use occlusion queries.** Portal visibility was tested with hardware occlusion queries
   (`86ac07f2f`, `57fe68c8a`). The pool was capped at 32k because "that's what MoltenVK can handle"
   (`ec4dd5d1c`).

**Inferred:** VKDoom never removed the CPU BSP walk. The levelmesh replaced per-frame geometry
generation, not visibility.

### 1.3 Bake

Per side and per sector, VKDoom ran the classic emitters into a capture structure:

- **Walls.** `DoomLevelMesh::CreateSide` (`doom_levelmesh.cpp:1346-1471`) calls `HWWall::Process`
  through `HWWallDispatcher(&doomMap, &result, lightmode)` with a VKDoom-extended `HWMeshHelper`.
  That helper has `opaque`/`masked`/`maskedOffset`/`translucent`/`translucentBorder`/`portals`
  buckets plus decals, and VKDoom added those buckets itself.
- **Flats.** `CreateFlat` (`:1473`) runs `HWFlat::ProcessSector` per section through
  `HWFlatDispatcher` into `HWFlatMeshHelper`.
- **Mesh builder.** The captured items are drawn into a `MeshBuilder`, a fake `FRenderState`
  that records vertices and indexes (`hw_meshbuilder.cpp`, added in `3fe8b6909`, 2022-12-28). The
  result becomes surfaces, uniforms and index ranges in a free-list allocator
  (`LevelMesh::AllocGeometry`, `hw_levelmesh.h:242`).
- **Data layout.** One big vertex buffer (`FFlatVertex`: position, lightmap index, uv,
  lightmap uv), a per-vertex *uniform index* stream, and a `SurfaceUniforms` buffer
  (`vk_levelmesh.h:134-146`). Each surface maps to a pipeline ID built from the material, shader
  and render state (`VulkanRenderDevice::GetLevelMeshPipelineID`, `vk_renderdevice.cpp:805`).
  Textures were bindless (`f550f5b05`).

### 1.4 Dynamic sectors and other updates

- **Re-bake on change.** Map-change hooks go through `UpdateLevelMesh`, an event interface
  (`src/levelmeshhelper.h`, `1d86d7080`, 2024-08-08). For example, `OnFloorHeightChanged` queues a
  full rebuild of the sector's flat **and every side of every line in the sector**
  (`doom_levelmesh.cpp:1080-1091`).
- **Rebuild at frame start.** `BeginFrame` re-runs `CreateSide`/`CreateFlat` for every queued item,
  then re-packs the lightmap atlas and re-uploads changed ranges (`:672-784`).
- **No interpolation in the mesh.** Moving planes produce a fresh bake every frame they move. Every
  frame of motion repeats a CPU re-bake plus upload.
- **Polyobjects** are rebuilt in full every frame: every `PolySides` entry gets
  `UpdateSide(..., Full)` in `BeginFrame` (`:689-694`).
- **Recorded symptoms of update cost:**
  - `27f0bea2b`: "Prevent levelmesh updates when ceiling/floor is moving at a zero speed".
  - `0b8989968`: "3D floor performance fix — prevent dummy trace sectors from sending updates to
    the accel struct".

### 1.5 Portals, sky, translucency, materials

- **Portals.**
  - Line portals are captured at bake (`result.portals`). Each frame they are re-pushed with
    `PutWallPortal` for seen sides (`hw_drawinfo.cpp:513-519`).
  - Sector portals come from `SeenSubsectorPortals`, and the BSP path leaves
    "`// To do: what to do here?`" for them (`hw_bsp.cpp:935, 952`).
  - Portal contents are drawn by the classic renderer (§1.1).
- **Sky.**
  - Sky walls and flats are baked as `LevelMeshDrawType::Portal` surfaces (`EFF_PORTAL`). Sky
    flats are pushed to z = ±32768 (`doom_levelmesh.cpp:1965-1986`).
  - The classic first-sky-portal clear is skipped when `gl_levelmesh` is on
    (`hw_drawinfo.cpp:1486`).
  - Sides with `HWF_SKYHACK` were "incompatible" and fell back (`:1427-1441`, `8d1061776`
    "Disable skyhack for level mesh").
- **Translucency.**
  - Any side with translucent or translucent-border walls sets `NeedsImmediateRendering`.
    It is then processed classically every frame through `di->ProcessSeg` (`:1438-1441`,
    `:662-668`). The comment says "For things that HWWall doesn't do correctly when drawn into the
    level mesh".
  - Translucent wall baking is commented out (`:1449-1461`).
  - Translucent flats were baked into a `Translucent` draw list.
- **Materials.** One pipeline per material/shader key, with draw ranges grouped by pipeline. There
  is no texture-array or atlas packing for world textures; bindless descriptors instead.
- **Dynamic lights.** Per-surface light lists live in a light-index buffer (`CreateLightList`).
  `lm_dynlights` (default false) controls whether dynamic lights go through the levelmesh light
  path (`doom_levelmesh.cpp:40`).

### 1.6 Performance and recorded problems

- **No benchmark numbers.** No numbers for large maps exist in any primary source I could reach.
  The issue tracker went with the repo. Commit messages only say "a bit" (`83441734d`, "Improve
  side and sector draw performance a bit").
- **Open problems recorded in code** (`doom_levelmesh.cpp`):
  - colormap and light-list provenance per uniform block ("To do: we need to know where each
    uniform block came from", `:1545`, `:1593`);
  - `SetColor` parity ("To do: calculate this correctly", `:1570`);
  - polyobject seg lookup ("Is there really not a better way of finding the subsector a polyseg
    resides in?", `:1370`);
  - model surfaces not updatable (`:488`).
- **Inferred** from the above and §1.1: the world path never reached parity or a performance case
  strong enough to turn on by default. It stayed a debugging and lightmap aid for about 22 months
  (2023-10 to 2025-08).

## 2. GZDoom

GZDoom's levelmesh never draws the world. `src/rendering/hwrenderer/doom_levelmesh.cpp` (387 lines,
added in `97073410c`, 2022-06-05, "Upload acceleration structures for the level mesh") builds a
standalone triangle soup of side and subsector surfaces. Its only consumer is the raytracing and
shadow backend: `hw_entrypoint.cpp:128` passes it to `screen->SetLevelMesh` →
`vk_raytrace.cpp:49`. There is no `gl_levelmesh` and no `DrawLevelMesh` in the tree.

## 3. HWMeshHelper's origin

- **GZDoom code, not VKDoom code.** `HWMeshHelper` and `HWWallDispatcher`'s mesh-helper mode were
  added by Christoph Oelckers in `9cd57faec` (2023-10-19, "rewrite of HWWalls to allow grabbing the
  output from the render item generator"). That commit sits on GZDoom master's first-parent line.
- **How VKDoom got it.** VKDoom merged it the same day (`e75e5a387`, "Merge remote-tracking branch
  'gzdoom/master' into merge-gzdoom") and then extended the struct (§1.3).
- **UZDoom's copy.** UZDoom inherited it through GZDoom. Its struct in
  `src/rendering/hwrenderer/scene/hw_walldispatcher.h` (`list`, `translucent`, `portals`, `lower`,
  `upper`) is byte-identical to GZDoom's, not VKDoom's.
- **Users.** It is unused in both GZDoom and UZDoom. **Inferred:** it was written to serve the
  VKDoom levelmesh effort, which was its only consumer.
- **What it relies on.** The capture works because `HWWallDispatcher` routes `AddWall`,
  `AddPortal`, and missing-texture calls into the helper when `di == nullptr`. Anything that reads
  `di` (fullbright scene, view-dependent state) must tolerate a null `di`, for example
  `isFullbrightScene()` returns false. The mesh-helper path still allocates wall vertices through
  `screen->mVertexData`, which is why ticket 06 lists "a path that produces vertices without
  `screen->mVertexData`" as a needed hook.

## 4. Helion, for contrast

Summary of `research/01-helion-levelmesh.md`:

- **Static mesh.** Helion pre-triangulates the whole level at load into static per-texture VBOs.
- **Dynamic sectors.** A per-plane dynamic bitmask flags sectors that move or change. Those sectors
  are linked into the render blockmap and zeroed out of the static VBO.
- **Per-frame CPU work.** Only the dynamic sectors in a distance- and 180°-limited blockmap region
  are re-emitted into a dynamic VBO. That VBO carries previous and current Z, and the GPU
  interpolates between them. When movement ends, the sector is re-baked into the static VBO.
- **Lighting** is per-sector data in a GPU buffer, not baked into vertices.
- **Portals** use flood-fill plus stencil.

Changes since `research/01` that matter here, all verified at `61c045a`:

- **The static mesh is drawn whole, with no culling at all.** In Static mode, `RenderGeometry`
  issues one `DrawArrays` per texture bucket over the entire static VBO
  (`Core/Render/OpenGL/Renderers/Legacy/World/Geometry/Static/StaticCacheGeometryRenderer.cs:859-892`).
  The blockmap walk (`LegacyWorldRenderer.IterateBlockmap`) handles only dynamic geometry and
  entities.
- **2D texture arrays (#1786, `d5938dc`).** Same-size textures are packed into arrays, so "most
  Doom maps … all floors and ceilings get packed into one array for a single draw call". The PR
  says it "drastically reduces draw calls and makes a massive difference on older iGPUs".
- **Adaptive mode (#1783, `eaafc65`)** answers brute force's weak spot on iGPUs.
  - A background thread (`Core/World/Impl/SinglePlayer/AutomapMarker.cs`) runs a CPU BSP walk
    with a view clipper at tick rate.
  - The walk marks automap lines and also times itself. The heuristic
    (`LegacyWorldRenderer.BspHeuristics.cs`) uses a median over a sample window, a switch count,
    a hysteresis band and a seg-count cap to choose between drawing the static mesh whole and
    rendering through the BSP.
  - The default is still `Static` (`ConfigRender.cs:93`).
- **Game-state feedback is asynchronous and off the render path.** Automap "seen" marking comes
  from that background thread, not the renderer, and is allowed to lag. This is directly relevant
  to ticket 18.
- **Transfer heights (#1727, `a8393b4`).** Upper and lower `TransferHeights` views use BSP
  rendering, "because the middle view is Helion's static mesh and the upper/lower views can
  potentially invalidate the entire map". The static mesh only serves the normal view.

## 5. Public GPU-driven renderers

**No public Doom-engine renderer with GPU cluster culling and indirect draws was found.** What I
searched:

- GitHub repository search for `doom gpu`, `doom renderer vulkan`, `doom source port vulkan`,
  `doom meshlet`, `doom gpu driven`, `doom indirect draw`, `doom compute culling`.
- The Doom ports already known here: GZDoom, VKDoom, Helion, and Raze (local clone).

The only Vulkan Doom ports found were VKDoom and an unrelated hobby repo. None of them cull on the
GPU.

**Ironwail (Quake) is the nearest public GPU-driven reference.** Verified in `Quake/r_world.c:56-110`
and `Quake/gl_shaders.h:1458-1600`:

1. **CPU input.** The CPU still computes the PVS (Quake's precomputed leaf vis) and uploads it as a
   bitset each frame.
2. **Reset.** The `clear_indirect` compute pass zeroes each per-texture
   `DrawElementsIndirectCommand.count`.
3. **Cull and compact.** The `cull_mark` compute pass runs one thread per leaf-surface pair. It
   tests the vis bit, the frustum (4 planes) and the surface backface plane. Survivors append
   their indices into a compacted index buffer with atomics on the per-texture command's `count`.
4. **Draw.** `MultiDrawElementsIndirect` draws the world, with one command per texture. Brush
   models use a separate `gather_indirect` remap.
5. **Driver workaround.** The shader comments note a workaround for old Intel drivers, which
   "error out on shaders that perform atomic operations on SSBO struct members". The fix was raw
   `uint` arrays.

Ironwail's design differs from this plan's in three ways:

- It culls per surface, not per cluster.
- It rewrites the index buffer, where this plan writes draw commands.
- It has no Hi-Z; visibility comes from precomputed PVS.

Doom maps have no PVS, which is why ticket 08 uses a two-phase Hi-Z instead.

## 6. Lessons for tickets 05–08

None of these is a VKDoom design to adopt. They are evidence about what fails and what works.

**Avoid (ticket 05 should test against these):**

1. **Don't keep the CPU BSP walk as the visibility source.** That is VKDoom's frame model (§1.2),
   and it left the CPU cost the levelmesh was meant to remove. The ticket-05 spike must measure
   culling with **no** CPU walk in the frame, otherwise it repeats VKDoom's result.
2. **Don't issue one CPU draw per surface or per range.** VKDoom's `drawIndexed` per range per
   pipeline (§1.2) scales with visible surface count. Tickets 07/08 should bound draws by bucket
   (`drawIndirectCount`), not by visible geometry.
3. **Don't re-bake geometry on every frame of plane motion.** VKDoom rebuilt the flat plus every
   side of every line in the sector, every frame a plane moved (§1.4), and both performance fixes
   it recorded were about update spam. The plane-anchored vertex plan (ADR 0003, ticket 09) avoids
   this. Ticket 05 should include a vantage with many moving sectors to confirm it.
4. **Don't let "fall back to classic" become the default for whole categories.** In VKDoom,
   translucent sides, sky-hack sides, polyobjects (rebuilt every frame) and every portal view all
   fell back to classic or immediate rendering (§1.5). Each fallback keeps both paths alive per
   frame. Tickets 12–16 should budget each category explicitly.
5. **Don't depend on hardware occlusion queries for portal visibility.** VKDoom hit a MoltenVK cap
   of 32k queries (§1.2). This plan's per-view cluster bitset is the replacement, and ticket 15
   should not reintroduce queries.
6. **Watch provenance gaps in baked lighting state.** VKDoom's unresolved "where did this uniform
   block come from" and `SetColor` parity TODOs (§1.6) are the same class of issue tickets 06 and
   10 face. Ticket 06 should record colormap and light-list provenance per surface at bake.

**Reuse (from GZDoom/UZDoom code, Helion and Ironwail, never VKDoom):**

1. **`HWMeshHelper` as it exists in UZDoom** is GZDoom code (§3). Ticket 06 may use it under ADR
   0003, extending it in this tree rather than importing VKDoom's extended version.
2. **Brute force is a valid baseline (Helion §4).** Ticket 05 should add a "draw the whole static
   mesh, no culling" arm next to GPU culling and classic. If a modern GPU draws everything faster
   than the cull costs on the ticket-02 vantages, the go/no-go result changes.
3. **Texture arrays for draw-call count (Helion #1786).** These complement descriptor indexing in
   ticket 07. The bucket count, not the texture count, should drive the number of indirect
   commands.
4. **Asynchronous, lagging game-state feedback (Helion `AutomapMarker`).** This supports ticket
   18's readback approach. Helion accepts tick-rate lag for automap marking, which bounds what DC3
   needs to allow.
5. **Ironwail's compute cull → compacted indirect draws (§5),** including the raw-`uint` atomics
   workaround. It is a reference for ticket 08's compute shape and for driver pitfalls, not for the
   culling granularity.
6. **Reduce scope for special views (Helion #1727).** Helion's static mesh serves only the main
   view, and the rest uses the general path. That matches ticket 08's "main view only" and argues
   for keeping portal views (ticket 15) out of the go/no-go.

## ADR 0003 overlap

ADR 0003 (bake by running the classic emitters through `HWWallDispatcher`'s mesh-helper mode) uses
**GZDoom code** that UZDoom already has (§3). Nothing in it comes from VKDoom. The *approach*,
running classic `HWWall::Process` and `HWFlat` into a capture at load, is the same idea VKDoom used
(§1.3). Two things differ:

- **Updates.** ADR 0003 bakes once and moves planes on the GPU through plane-anchored vertices.
  VKDoom re-baked on every change.
- **Flats.** ADR 0003 takes flats from the classic static vertex builder (`hw_vertexbuilder.cpp`),
  not from `HWFlat::ProcessSector`.

Whether that overlap is acceptable under the no-VKDoom-architecture rule is a user decision. It is
recorded in ticket 04's Comments.
