# 09: Dirty state stream + plane warp (moving sectors)

**Map:** `../map.md`: Data model (State buffers, Plane-anchored vertices), Frame model (CPU step 1).
**Type:** task
**Status:** resolved
**Blocked by:** 08

## What to build

- **Dirty tracking for sector state** (planes, `TexZ`, light ingredients, colormap, glow, flags).
  - Inside the `D_Render` interpolation window (after `DoInterpolations`), compare each candidate
    sector's packed record with its last-uploaded copy and upload only the changes.
  - Candidates: sectors touched by interpolation (the interpolator's list), light thinkers, and
    explicit change hooks where they are cheap. Otherwise use a full compare (a full compare is
    O(sectors) of memcmp, which is acceptable only if ticket 02 / 05 numbers say so; measure it).
  - Non-interpolated frames upload raw tick values.
- **Restore after interpolation.** `RestoreInterpolations` moves planes back. The next frame's
  compare catches that, so no extra coupling is needed. Uploaded records must be the interpolated
  values used for that frame.
- **Cluster z-range refresh.** When a sector's planes change, recompute the z-ranges of its
  back-linked clusters and upload them dirty-only. The cull must stay conservative mid-frame.
  - Ticket 05 findings:
    - Upload through a host-visible update list plus **one compute scatter** dispatch. Do not use
      a `VkBufferCopy` region per record: those cost 0.16–0.30 ms of GPU time per frame on Pirates
      MAP49, where two moving planes of big sectors fan out to 508–966 clusters.
    - Expect that fan-out. With the scatter, 1796 dirty clusters cost 0.006 ms GPU and 2000
      moving planes about 0.1 ms CPU.
    - The moving planes per frame are exactly the interpolator's `DSectorPlaneInterpolation`
      list.
- **Warp everywhere.** Doors, lifts, crushers, scrollers' plane `TexZ`, transdoor (z−1 floors, from
  the bake), and sector_link-moved sectors all move through the same plane-anchored path.
- Measure the upload volume and CPU time per frame in `r_perflog`.

## Acceptance

- [x] DOOM2 MAP01 doors and MAP07 lift, on ticks with a door open or a lift mid-transit, plus demo
      replay: A/B passes (fullbright override until ticket 10). Expected classes DC1 / DC2.
      (MAP01 tour replay: fails only on out-of-scope classes, see Comments.)
- [x] Per-frame upload bytes logged; a static scene uploads ~0 bytes.
- [x] No culling holes on fast movers (a scripted crusher and lift test map, or the DOOM2 MAP30
      lifts).

## Comments

- 2026-10-13 (from 06): the shader must evaluate `(d + nx*x + ny*y) * negiC + zOffset` in exactly
  the order of `LM_PlaneZ` (`hw_levelmeshbake.h`). Use no fma contraction, or bit-exactness breaks.
  - `LevelMeshSurface::planeRefs[4]` overflows on 0.1–0.4 % of surfaces (2474 on myhouse MAP01).
    Derive dirty sets from the vertex anchors (plane → clusters built at upload), not from
    `planeRefs`.
  - About 3.4k vertices round-trip within 1 ulp but not bit-exactly (myhouse MAP01 most of them).
    Accepted as passing (user decision); switch to a formula that is exact at rest only if A/B
    shows seams there.
- 2026-10-13 (from 07):
  - The sector→cluster CSR (`LevelMeshBake::SectorClusterStart/SectorClusters`) is built from
    the vertex anchors, as asked above. The max fan-out is 322 on myhouse MAP01 and 519 on
    planisf MAP01.
  - GPU state buffers exist and are filled at upload: `LevelMeshSectorState` (planes, texZ,
    lights), `LevelMeshSideState` (side×tier), `LevelMeshPlaneXform`. Their `reserved` fields are
    free for this ticket and ticket 10.
  - Uploads go through `VkLevelMesh::AllocUpload` (host-coherent, `STORAGE` usage, so a
    compute scatter can read them) plus `QueueCopy` / `FlushUploads`.
  - The cluster→sector list is uploaded as well (binding `LMBIND_CLUSTERSECTORS`).
- 2026-10-14 (from 08): the draw is `VkLevelMeshRenderer` (`vk_levelmeshrenderer.{h,cpp}`).
  - **Cluster bounds must follow the planes.** `cull.comp` tests the cluster AABB, whose z is
    built from the load heights. Once the vertex stage reads moving planes, a door, lift or
    crusher can leave its cluster's box, and frustum or Hi-Z culling will drop visible geometry.
    Refresh `bmin.z / bmax.z` from the dirty sectors (`LMBIND_CLUSTERSECTORS` gives the
    sector→cluster fan-out), or widen the boxes to each sector's movement range.
  - Plane z comes from `lmSectors[].planes` and `texZ` in `levelmesh.vp`. Writing those per
    frame is the only change the vertex stage needs.
  - Latent ownership issue: deferral is propagated to whole side tiers, but not to whole sector
    planes. All of today's plane deferral conditions are per sector, so nothing breaks yet, but
    a per-subsector deferral would make classic and the levelmesh both draw the plane.
  - In the 08 demo A/B, Pirates MAP43 tick 120 drew an upper wall against the sky that classic
    did not. That is either a moving sector or a sky rule (14); recheck it here.
- 2026-10-14: resolved.
  - **Code.**
    - `src/rendering/hwrenderer/hw_levelmeshstate.cpp` (new): `LevelMesh_UpdateState`, called
      from `RenderViewpoint` right after `LevelMesh_UpdateResources`, inside the `D_Render`
      interpolation window. It packs each candidate sector (`LevelMesh_FillSectorState`, the
      single packer, moved here from the resources file), memcmps it against the CPU mirror
      `LevelMeshBake::Stream.Sectors` (the records last handed to the backend), and streams
      only the differences. Interpolated frames upload interpolated values; the next frame's
      compare catches `RestoreInterpolations`.
      - It runs for every viewpoint (camera textures too); `LevelMesh_BeginFrame` (from
        `RenderView`) marks the frame boundary, so the change lists rotate once per frame
        and a camera viewpoint cannot drop the previous frame's changes.
      - If the backend uploads nothing (returns 0 bytes), the mirror and cluster boxes are
        rolled back, so the changes are found again by the next update.
    - **Candidates** (a full compare is too slow, see Measurements):
      - the sectors changed by this and the previous frame (catches the last frame of a
        move);
      - every `DSectorPlaneInterpolation` (`FInterpolator::ForEachMovedSector`, a new
        `DInterpolation::MovedSector()` virtual);
      - the sectors of every `STAT_SECTOREFFECT` and `STAT_LIGHT` thinker (movers that do not
        interpolate, such as Hexen / Strife animated doors, and light thinkers);
      - the sectors reported by change hooks: `LevelMesh_SectorChanged`, called from
        `sector_t::SetLightLevel` / `ChangeLightLevel` / `SetGlowColor` / `SetGlowHeight`
        and `::SetColor` / `::SetFade` (ACS, light specials, ZScript setters). A no-op
        without a levelmesh; copies made by the fake-flat code carry the original index, so
        at worst they add a spurious candidate;
      - a round-robin backstop sweep of `lm_statesweep` sectors per update (default 512), for
        state that scripts write to fields directly. Its latency is bounded (myhouse MAP01: 70 frames
        for a full pass), not zero.
    - **Cluster z refresh.** The cluster builder now records, per cluster, every anchor plane
      with its zOffset range (`ClusterPlanes` / `ClusterPlaneStart`, CPU only). When a
      sector's planes change, the clusters on its `SectorClusters` fan-out get a new z-range:
      the anchor planes over the cluster's xy box plus the offset ranges. This is exact for
      level planes; sloped planes are evaluated at the box corners and padded by 1 unit.
      `bake->Clusters` is kept in sync on the CPU.
    - **Upload.** `DFrameBuffer::UpdateLevelMeshState(LevelMeshStateUpdate)` goes to
      `VkLevelMesh::UpdateState`. The sector indices, records and cluster z entries share one
      staging-ring allocation, and **one** `scatter.comp` dispatch writes them into the
      sector-state and cluster buffers (no `VkBufferCopy` regions). It is recorded into the
      transfer command buffer, which is submitted ahead of every draw of the frame, between an
      `ALL_COMMANDS` → compute barrier and a compute → `ALL_COMMANDS` barrier. Each ring slot
      has its own scatter descriptor set, replaced when that slot's buffer grows.
    - **Sector record** grew from 16 to 20 words: `lightColor` / `fadeColor`, `colormap`
      (desaturation | blend factor << 8 | fog density << 16) and per-plane `glowColor` /
      `glowHeight`. No sector flags are carried yet: ticket 10 adds what shading needs.
  - **Two classic behaviours a moving plane exposed** (both found by the movers A/B):
    - **Crossed tiers.** The classic stops emitting an upper tier once the back ceiling rises
      past the front ceiling, and a lower tier once the back floor drops past the front floor.
      The mesh quad instead turned inside out and, seen from the other side, z-fought with
      (and beat, as it is drawn first under `DF_Less`) the classic's other-side tier. This
      showed on MAP07's rising stairs.
      - Fix: `LevelMeshSurface` grew from 24 to 28 words, with `edgeTop` plus `reserved[3]`.
        `edgeTop` is the classic upper bound of the tier: front ceiling for upper and
        one-sided walls, back floor for lower walls, none for two-sided mids and 3D-floor
        sides.
      - `levelmesh.vp` clamps every non-top vertex to it, so a crossed tier collapses.
        `LevelMeshBake::VertexZ` mirrors the clamp.
      - The bake leaves `edgeTop` unset where a non-top vertex already lies above it at load
        (stat "unbounded wall tops": 0 on DOOM2 / Hexen / Heretic / SOS / planisf, 802 of
        176k walls on myhouse MAP01, 182 on Pirates MAP50). At rest the clamp is a no-op by
        construction.
      - The cluster builder adds each wall's `edgeTop` to its cluster planes.
    - **Dynamic clamp-Y.** The classic sets `HWF_CLAMPY` only while a wall's v range lies
      within [0, 1] (`CheckTexturePosition`, from the current heights). The bake froze the
      load-time flag, so a clamped wall that a lift stretched showed edge-streaks (MAP01 lift
      shaft).
      - Fix: `lm_frag.glsl` (`LM_SAMPLER`) drops the Y clamp for fragments whose v is outside
        [0, 1].
      - Remaining nuance: with linear filtering, a stretched wall's edge texels still sample
        clamped inside [0, 1] where the classic would wrap. This is a sub-texel edge
        difference (DC2-like) and does not show with the A/B pins.
      - The reverse case (a wall that loads wrapping and later shrinks into [0, 1], where the
        classic would clamp) keeps wrapping. Same nature: identical with nearest filtering.
      - The sampler bit is passed as `LM_SAMPLER_CLAMPY` (static_assert on the clamp enum).
  - **Verification** (Vulkan, clang RelWithDebInfo, fullbright).
    - **New tooling:**
      - `make_demo.py --path script`: steps `warp`, `face`, `use` and `special`
        (`DEM_RUNSPECIAL`).
      - `ab_manifest_movers.json` with `demos/movers/DOOM2_MAP{01,07,30}_MOVERS.lmp`, captured
        every 5 tics. The demos open MAP01's start-area door by use and run its tag-5 lift and
        a crusher; raise MAP07's stairs and run its four lifts and a crusher; on MAP30, run all
        four lift tags at speed 255, a speed-255 crusher, and a 600-unit raise plus 900-unit
        lower.
    - **Classic vs levelmesh, movers demos:**
      - MAP01: 33 identical + 47 pass.
      - MAP07: 80 pass.
      - MAP30: 71 of 80 ticks fail on its animated walls and flats (ticket 11), already from
        the first tick, before anything moves.
    - **Classic vs levelmesh, tour demos:**
      - DOOM2 MAP07: 29 identical + 58 pass.
      - DOOM2 MAP01: 20 fails, all animated `FWATER` or out-of-bounds tour views (classic
        draws sky-hack ghosts behind the walls). The perflog of that demo shows zero state
        uploads, so this ticket cannot affect it. Classes: tickets 11 and 18.
    - **Pirates MAP43 demo:**
      - Same fail ticks with the stream on and off, minus the moving-sector ones: freezing it
        (`lm_stateupdate 0`) adds fails at ticks 930, 1380, 1530, 1860 and 2340.
      - **Tick 120 (the 08 finding) reproduces with the stream frozen:** it is a sky rule
        (ticket 14), not a moving sector.
      - Candidates vs `lm_statefull 1`: 84 identical + 3 pass. A same-config levelmesh pair
        gives 82 + 5, so that is launch noise.
    - **Culling holes:** levelmesh vs levelmesh, `lm_cullmode 0` against `2`, on the three
        movers demos: 80/80 identical each, at cluster budget 256 and at 16.
      - Negative control (`lm_clusterz 0` on B, budget 16): MAP01 fails 17 ticks while its
        door opens, so the test detects stale boxes.
      - MAP07 / MAP30 stay identical even without the refresh. Their movers never leave the
        boxes in a culled way: lifts move within neighbour heights, and the MAP30 raise stays
        in view. A MAP30 variant with the view next to the 600-unit raise and the 900-unit
        lower also stayed identical with `lm_clusterz 0`, so the box refresh is proven by the
        MAP01 door only.
    - **Static regression (classic vs levelmesh):** DOOM2 MAP07 30/30 identical; MAP01, MAP31,
      Pirates MAP43 and Heretic E1M2 30/30 pass. Same as ticket 08.
    - **Validation:** `lm_bake_survey.py --vkdebug`, 26 maps. Only the known VUIDs
      (`vkDestroySemaphore-05149`, `vkDestroySwapchainKHR-01282`, myhouse
      `VkWriteDescriptorSet-04150`).
  - **Measurements** (perflog v4, vantage set, tics ≥ 350; `logs/lm09_*`).
    - **Static scenes upload 0 bytes at p50:** DOOM2 MAP27/31, myhouse 20PAM / MAP01,
      planisf V1/V2, SOS MAP32 V2.
    - **Where uploads are real:**
      - SOS MAP32 V1: 65 sectors / 4.4 KB per frame, light thinkers flickering.
      - Pirates MAP49: 2 perpetual movers, 68 clusters, 1.2 KB.
      - Pirates MAP54: up to 8 sectors and 27 clusters.
    - **Update CPU time, p50 / p95:**
      - DOOM2 0.001–0.003 ms (every sector is a candidate);
      - myhouse MAP01 0.027 / 0.038 ms (512 candidates out of 35,853 sectors);
      - planisf 0.025 / 0.034 ms;
      - SOS MAP32 0.031–0.038 / 0.038–0.051 ms.
    - **Full compare** (`lm_statefull 1`):
      - myhouse MAP01 1.18 / 1.34 ms (all 35,853 sectors); SOS MAP32 0.28 / 0.34 ms;
        planisf 0.16 / 0.20 ms.
      - That would use B3's whole 1 ms budget on myhouse, so it stays a diagnostic. It found
        the same dirty counts as the candidates on these vantages.
    - **Scatter GPU time** is not timestamped (it sits in the transfer commands). Ticket 05
      measured 0.006 ms for 1,796 clusters.
  - **Debug cvars** (not saved): `lm_stateupdate`, `lm_statefull`, `lm_statesweep`,
    `lm_clusterz`.
  - **Code review follow-ups** (standards + spec review): change hooks and glow added (above);
    per-frame change-list rotation; mirror rollback on a failed upload; stream bookkeeping
    moved into `LevelMeshStateStream`; `PlaneRefZ` and `LM_Span` helpers; `ScatterConstants`
    static_assert + keep-in-sync notes; banner and comment fixes. Rechecked after the fixes:
    movers A/B (classic vs levelmesh: same as above), culling holes at budget 256 / 16 with
    the negative control, static DOOM2 MAP07 / Pirates MAP43, the vantage perf matrix (11/11
    pass, `logs/lm09b_*`, same costs) and the `--vkdebug` survey (known VUIDs only).
  - **Known limits:**
    - Script writes that bypass both the setters and the candidates (ZScript / ACS writing a
      plane or field directly, outside a thinker) reach the GPU only through the sweep.
    - The upper-tier `bch1a` rule (the bottom becomes the front floor when the back ceiling
      sinks below it) is not modelled; only crossing past `edgeTop` is.
    - Plane deferral is still per sector, not per whole plane (08 note); nothing deferred per
      subsector yet.
