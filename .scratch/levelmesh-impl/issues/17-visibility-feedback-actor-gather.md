# 17: Visibility feedback + actor/decal gather

**Map:** `../map.md`: Frame model (GPU step 7; CPU step 3), Acceptance policy DC3;
`../research/02-consumer-inventory.md`.
**Type:** task
**Status:** resolved
**Blocked by:** 15

## What to build

Replace the BSP walk's role as the source of "what is visible" for CPU consumers:

- **Readback ring.** Each view's per-cluster visibility bitset (and per-surface bits where
  consumers need them) is copied to a host-visible ring of `HW_MAX_PIPELINE_BUFFERS` slots and read
  when its fence signals. Latency = the pipeline depth. Document the exact frame latency (feeds
  DC3).
- **Actor gather.** This replaces `RenderThings` / `RenderParticles` being called per visited
  subsector.
  - Per view, walk the actors (and particles) of subsectors in **visible-or-adjacent** clusters
    from the newest available readback, intersected with the current frustum.
  - Feed survivors into the existing `HWSprite` / model paths, unchanged. The `GLDL_*` lists and
    `DrawSorted` stay as they are.
  - Over-inclusion is harmless (z-buffered). Under-inclusion produces a DC3 pop-in on newly revealed
    areas. Including adjacent clusters (the cluster adjacency graph from the bake) must keep this
    bounded; measure it.
- **Decal gather.** Decals of visible sides (same source) go to the existing decal draw.
- **Dynamic-light list inputs** for sprites and models stay classic (ticket 19 covers walls / flats).
- **Shared order.** Portal views (ticket 15) use the same gather with their own bitsets.

Ticket 05: the visibility must be occlusion-accurate (two-phase Hi-Z). A frustum-only set
over-includes massively: on myhouse MAP01 V1, 2104 frustum clusters against 76 Hi-Z-visible. That
would bloat the CPU sprite gather and the ticket-18 exposure marks.

## Acceptance

- [x] Demo A/B with monsters on DOOM2 MAP01 / MAP23 and a slaughter map: actor pixels pass, apart
      from DC3 clusters. The DC3 count and its duration in frames are measured and reported.
- [x] Gather CPU time scales with actors in view, not with level size (stress vantages).
      (Measured: it scales with the gathered clusters / subsectors, i.e. the view; see Answer.)

## Comments
- 2026-10-18: decal gather moved to ticket 20 (user decision, like 16's bake-driven sets): decals
  stay on the walk's `HWWall`s until the walk is gone. Dither / exposure / out-of-body views stay
  classic (ticket 18).

## Answer

**Transitional, like 14 / 15.** On the levelmesh path a view takes its actors and particles from
last frame's GPU cluster visibility; the BSP walk still runs for everything else and only stops
calling `RenderThings` / `RenderParticles`. `HWSprite`, models, `GLDL_*` and `DrawSorted` are
untouched; `RenderThings` keeps its distance / radius / map-section rules and the
`thing->validcount` dedup.

**Latency: exactly one rendered frame.** `VulkanRenderDevice::Update` waits for every submit
(`WaitForCommands(true)`) before the next frame begins, so the gather at frame N reads frame N-1's
final cull. The ring depth (8 slots) is not the latency. A view without a readback gathers through
the walk for that frame: first frame of a level, a frame without a levelmesh draw (`lm_draw 0`,
menus, wipes), a portal view past the 64-view limit, a stencil-less portal view, out-of-body views
(18), offscreen views (camera textures, save pictures).

**What changed:**
- **Bake** (`hw_levelmeshclusters.cpp`): per cluster its BSP leaves (`ClusterLeafStart` /
  `ClusterLeaves`; a chunked leaf is listed by each of its chunks, a subtree group by every cluster
  it produced, sky / polyobject clusters have none), the inverse map, and the cluster adjacency
  (`ClusterAdj`: chunk siblings, plus the clusters of the leaves across each seg's partner).
  Linear in segs; `LevelMeshClusters` prints `adjacency N (max M)`.
- **GPU** (`cull.comp`): phases SECOND / SINGLE `atomicOr` the visible clusters into a per-view
  bitset (binding 6), cleared before each view's culls. `VkLevelMeshRenderer` copies it after the
  final cull into a host-visible ring (`mVisReadback`, slot × (1 + 64 views) × words), recording
  the view's key per slot. `BeginFrame` invalidates the previous frame's slot;
  `DFrameBuffer::LevelMeshVisibility(levelId, viewKey, words)` returns a view's bitset (GL: null).
- **View key** (`HWDrawInfo::LevelMeshViewKey`): 1 for the main view; a portal view hashes its
  parent's key, its portal's source, type and mirroring. Writer (`DrawLevelMesh`) and reader use
  the same function. In practice the portaltest line portal reads back its own key every frame.
- **Gather** (`hw_bsp.cpp`, `BeginLevelMeshGather` / `GatherLevelMeshThings`, after the walk,
  through the same worker jobs): visible clusters, then (`lm_gatheradjacent`) their neighbours;
  each tested against the current frustum (`LevelMesh_ClusterInFrustum`, `cull.comp`'s
  `ProjectBox` rejection on the CPU); their leaves get `DoSubsector`'s rejections (map section,
  polyobject origin, clip portal) and then `RenderParticles`, and `RenderThings` once per sector.
  Dedup by per-bake stamps sized once at clustering, so a gather costs what it takes.

**Results** (Vulkan, demo mode). "Isolation" = A `gl_uselevelmesh 1; lm_gather 0` (walk gathers)
against B the default (GPU gather), so only the gather differs.

| A/B | run | failing ticks |
|---|---|---|
| gathertest isolation, all 4 maps (budget 256) | `20261018T222821Z-ab` | 0 / 934, every tick identical |
| gathertest isolation, `lm_cluster_budget 16` (36 / 128 / 1 / 2001 clusters) | `20261018T223707Z-ab` | MAP01 1 (tick 573), MAP02 1 + MAP03 1 (4-px specks) |
| same, B `lm_gatheradjacent 0` | `20261018T231725Z-ab` | MAP01 1 (tick 573) |
| monsters (`ab_manifest_gather_monsters.json`) isolation | `20261018T231921Z-ab` | DOOM2 MAP01 1/87, MAP23 0/87, SOS MAP32 4/87 |
| same, B `lm_gatheradjacent 0` | `20261018T234211Z-ab` | MAP01 1 (+1 within tolerance), MAP23 0, SOS 9 |
| gathertest classic vs default | `20261018T225936Z-ab` | MAP01 86, MAP02 0, MAP03 / 04 36 / 36 |
| gathertest classic vs `lm_gather 0` | `20261018T230819Z-ab` | MAP01 86 (same set), MAP02 0, MAP03 / 04 35 / 36 (one-tick flakes) |
| classic determinism, DOOM2 MAP01 | `20261019T000257Z-det` | 0 / 87 (`gl_uselevelmesh 0` unchanged) |

- **DC3 (pop-in), counted on the isolation pairs:** gathertest at budget 16: 1 tick (573, the
  camera jumping back into a monster room; all four monsters missing for one frame, the next tic
  identical; the other two jumps did not pop). SOS MAP32: 4 of 87 ticks. Dense captures
  (`20261018T232850Z-ab`) show bursts during the tour's fast turns (718-721, 1106-1109,
  1829-1830, 2395-2400): ≤ 100 px each, all distant sprites on the leading screen edge, a
  different set each tic, so each sprite pops for one frame. Max duration 1 frame. The
  candidates are staged in `allowlists/pending/SOS_MAP32.json` (12 clusters, class
  `UNREVIEWED`) for the human review / promote step.
- **Adjacency is doing work:** turning it off takes SOS from 4 to 9 failing ticks.
- **Not DC3, DOOM2 MAP01 2180-2193 (14 frames):** the gather draws *more* monsters than the walk.
  They stand on the outdoor field the levelmesh draws but the classic walk never visits (classic
  vs levelmesh already differs there by 26k px, ticket 23's class). Both pixel sets are
  "correct" for their geometry; this goes with 23 / 20.
- The 86 / 36 gathertest failures against classic are the same set with the gather off: specks of
  the existing levelmesh geometry (≤ 3.9k px), not actors.
- Rooms: pit (upper body only), particles (in view, behind a pillar), line portal, mirror and
  sector stack with monsters on both sides are identical in isolation; the levelmesh's portal
  views gather from their own bitsets.
- Validation layers (`vk_debug 1`, DOOM2 MAP01, portaltest, gathertest MAP01): only the known
  swapchain-recreate pair (`vkDestroySemaphore-05149`, `vkDestroySwapchainKHR-01282`). GL backend
  runs gathertest without gathering.

**Perf** (perflog v8 `lmgv lmgc lmga lmgs lmgsec lmgms`; monsters on, `gl_uselevelmesh 1`,
tics 105-1050, p50 / p95; `logs/T17*`):

| vantage | gather ms | clusters (adj) | subsectors | sprites: walk → gather | `ss` ms | `sr` ms | `r` p50 ms |
|---|---|---|---|---|---|---|---|
| gathertest MAP03 (1 sector) | 0.000 / 0.000 | 1 (0) | 1 | 23 → 23 | 0.004 → 0.004 | 0.031 → 0.030 | 0.105 → 0.105 |
| gathertest MAP04 (+2000 rooms) | 0.000 / 0.001 | 1 (0) | 1 | 23 → 23 | 0.004 → 0.004 | 0.032 → 0.031 | 0.130 → 0.129 |
| gathertest MAP02 arena | 0.001-0.002 | 2-8 | 33-146 | 30-376 → 376 | 0.05 → 0.05 | 0.47 → 0.48 | – |
| SOS MAP32 V1 | 0.134 / 0.150 | 234 (91) | 4547 | 2514 → 589 | 1.095 → 0.222 | 2.746 → 0.735 | 21.29 → 19.40 |
| myhouse MAP01 V1 (3 views) | 0.281 / 0.323 | 1065 (495) | 4005 | 32 → 103 | 0.022 → 0.041 | 0.071 → 0.156 | 6.73 → 6.79 |
| myhouse MAP01 V2 (3 views) | 0.289 / 0.376 | 864 (371) | 4505 | 53 → 114 | 0.024 → 0.048 | 0.085 → 0.179 | 6.47 → 7.06 |

- **Level-size independent:** MAP03 and MAP04 (2001 sectors, 128 clusters) gather the same one
  cluster at the same cost. The gather's own cost follows the gathered clusters / subsectors
  (the view), not the actor count: the arena's 371 actors cost 0.002 ms to gather; their cost is
  the existing `ss` / `sr`. All well under the 1.0 ms B3 budget.
- **Granularity cuts both ways.** On SOS V1 the occlusion-accurate set drops the processed sprites
  from 2514 to 589 (`ss + sr` -2.9 ms, `r` -1.9 ms). In myhouse's tight interiors clusters are
  coarser than the walk's clipper: 2-3× the sprites (all z-rejected, pixels identical) and
  ~0.4 ms extra (`lmgms` 0.28 plus `ss` / `sr`). The arena is one sector, so any gathered piece of
  it draws all its things (`RenderThings` is per sector, as in the walk). Finer gathering (per
  subsector bits, per-thing culling against the cluster boxes) is an option for ticket 20 if the
  perf bars need it.
- An option for the turn pop-in if DC3 ever needs to shrink: have the cull also mark clusters in a
  guard band just outside the frustum (no occlusion info there) so a turn's leading edge is
  gathered one frame early.

**Test kit:** `make_gathertest.py` (committed separately; abtest README). `abtest.py` keeps
monsters for a manifest with `"monsters": true` and accepts per-map `demo_ticks` with `dense`
ranges. `ab_manifest_gather_monsters.json`: DOOM2 MAP01 / MAP23 and SOS MAP32 tours with
monsters. `run_matrix.sh` mounts `GATHERTEST_*` demos.

**Debug cvars:** `lm_gather 0` (the walk gathers; the A side), `lm_gatheradjacent 0`,
`lm_gatherlog 1` (one `LMGATHER` line per view).
