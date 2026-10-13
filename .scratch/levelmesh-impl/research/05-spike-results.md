# Spike results: GPU-driven culling go/no-go

Research for ticket 05. Question: on the ticket-02 stress vantages, does GPU-driven cluster culling
plus indirect drawing of the static world beat the classic path by enough to justify tickets 06–21?

**Recommendation: go-with-changes.** The details are in § Recommendation. The user decides.

## Sources and setup

| Item | Value |
|---|---|
| Spike code | branch `spike-05-gpucull` (throwaway, never merged), commit "spike: GPU cluster cull + indirect draw prototype (ticket 05, throwaway)" |
| Raw data | on the spike branch: `tools/abtest/results/spike05.json`, `spike05-tables.md`; logs in `tools/abtest/logs/s05*` (gitignored) |
| Machine | the ticket-02 reference: Ryzen 9 5900X, RX 7900 XTX (RADV), 2560×1440, vsync off, RelWithDebInfo |
| Protocol | ticket-02 v2: `baseline.cfg`, `run_matrix.sh`, the 11 vantage demos, window 105 + 945 tics. Every arm is Vulkan vs Vulkan. The classic arm was re-measured on the spike build and matches baseline v2 within ~1 % (SOS MAP32 V1 wall p50 48.4 vs 47.9 ms). |

### What the spike builds

- **Geometry.** The classic `DoomLevelMesh` (walls per sidedef, flats per subsector, 3D-floor
  planes), with positions in GL world space and a surface id per vertex. There are no texture
  coordinates. It differs from the classic draw: no two-sided mid textures, whole-sidedef walls,
  and some surfaces the classic never draws (see Caveats).
- **Clusters.** Each wall goes to the subsector of its side's first seg; flats go to their own
  subsector. A BSP subtree with at most `r_spike_budget` triangles becomes one cluster, and a
  leaf over budget is split into chunks. The index buffer is ordered by cluster. Each record holds
  a 3D AABB and an index range. Sector → cluster back-links are kept for moving planes.
- **Cull.** One compute shader, one thread per cluster.
  - Frustum: the 8 AABB corners in clip space, with no far plane (the classic pipeline uses depth
    clamp).
  - Hi-Z: max-depth pyramid, picking the mip where the projected rect covers ≤ 2×2 texels.
  - Commands are raw `uint` `VkDrawIndexedIndirectCommand`s plus atomic counts, drawn with
    `vkCmdDrawIndexedIndirectCountKHR`.
- **Modes** (`r_spike`):
  - `1` two-phase Hi-Z. Phase 1 draws the clusters visible last frame that pass the frustum. The
    Hi-Z pyramid is then built from that depth. Phase 2 tests everything, frustum + Hi-Z, and draws
    the newly visible clusters. Phase 2 also updates the persistent per-cluster visibility.
  - `2` frustum-only.
  - `3` no cull: the whole mesh in one indirect command, Helion-style.
- **Mesh-only frame.** In the main view, `HWDrawInfo::DrawScene` skips `CreateScene`,
  `RenderFirstSkyPortal` and `RenderScene` and draws only the spike mesh. The classic BSP walk is
  not in the frame (research 04 avoid-1).
- **Shading.** Flat per-surface hash color, back-face culled (front = clockwise in the
  `DoomLevelMesh` winding). `r_spike_fsload N` adds N iterations of synthetic per-fragment ALU work,
  as a stand-in for real shading cost.
- **Measurements.** These are new `r_perflog` columns.
  - GPU timestamps per phase (cull 1, draw 1, Hi-Z, cull 2, draw 2) and the scene span `gsc`. In
    the classic arm `gsc` brackets `DrawScene`.
  - Counter readback: clusters drawn per phase, clusters passing the frustum, triangles.
  - All of these lag one frame.
- **Moving sectors.** `r_spike_movers N` starts perpetual lifts (±48 units, speed 4) on the N
  sectors nearest the camera at maptime 35, inside the sim tick.
  - Each frame the moving planes come from the interpolator's `DSectorPlaneInterpolation` list.
  - Their back-linked clusters get a refreshed z-range. The geometry stays at its load heights.
  - The dirty records go to a host-visible list, and one compute dispatch scatters them into the
    cluster buffer.
- **Correctness check.** Pixel compare in the A/B capture mode. Hi-Z output matches frustum-only
  and no-cull output on the SOS MAP32 V1 vantage (3 ticks). On the moving SOS MAP32 tour, one sim
  tic per frame and 13 captures, it differs by ≤ 3 pixels per frame (z-fight order). The same holds
  with MSAA 4× (`gl_multisample 4`). Two-phase Hi-Z adds no pop-in.

## Results

All times in ms, p50 unless marked. `wall` is the real frame interval (the bar metric). `r` is the
CPU render window. `gsc` is the GPU scene span. Cluster budget 256 unless stated.

### Frame time vs classic (the bars)

| vantage | classic wall p50 / p95 | Hi-Z wall p50 / p95 | wall p95 ratio | classic r p95 | Hi-Z r p95 |
|---|---|---|---|---|---|
| SOS MAP32 V1 | 48.37 / 51.50 | 0.498 / 0.737 | 1.4 % | 49.00 | 0.065 |
| SOS MAP32 V2 | 23.00 / 25.84 | 0.482 / 0.717 | 2.8 % | 23.89 | 0.061 |
| planisf V1 | 27.41 / 30.67 | 0.464 / 0.547 | 1.8 % | 29.06 | 0.053 |
| planisf V2 | 24.09 / 26.63 | 0.462 / 0.537 | 2.0 % | 25.15 | 0.051 |
| Pirates MAP54 V1 | 7.75 / 9.18 | 0.438 / 0.520 | 5.7 % | 8.35 | 0.057 |
| myhouse MAP01 V1 | 7.32 / 11.18 | 0.563 / 0.759 | 6.8 % | 8.48 | 0.066 |
| myhouse MAP01 V2 | 7.70 / 12.04 | 0.736 / 0.823 | 6.8 % | 9.38 | 0.072 |
| Pirates MAP49 V1 | 3.87 / 4.57 | 0.492 / 0.720 | 15.8 % | 3.68 | 0.067 |
| myhouse 20PAM V1 | 1.02 / 1.21 | 0.416 / 0.491 | 40.7 % | 0.54 | 0.049 |
| DOOM2 MAP27 V1 | 0.83 / 0.96 | 0.420 / 0.494 | 51.5 % | 0.24 | 0.050 |
| DOOM2 MAP31 V1 | 0.48 / 0.71 | 0.422 / 0.498 | 69.8 % | 0.11 | 0.051 |

- **B2 (heavy vantages, wall p95 ≤ 50 % of classic):** passes on all seven heavy vantages, at
  1.4–6.8 %.
- **B3 (render CPU excl. sprites p95 ≤ 1.0 ms):** 0.05–0.07 ms everywhere, against a classic
  maximum of 49 ms. It does not grow with level size.
- **B1 (small maps):** the mesh-only frame is already below classic on the small vantages.
  - The levelmesh's fixed per-view GPU cost is about 0.08 ms (`gsc` on DOOM2 MAP31: 0.088
    two-phase vs 0.055 frustum-only vs 0.075 classic).
  - It is under the bar's 0.10 ms additive slack, so B1 stands as written.
- These are **mesh-only frames**. The end-state projection below adds back what the spike leaves
  out.

### Cluster budget sweep (two-phase Hi-Z)

GPU scene span `gsc` p50 / clusters / clusters drawn / triangles drawn:

| vantage | 64 | 128 | 256 | 512 |
|---|---|---|---|---|
| SOS MAP32 V1 | 0.120 / 14089 / 2406 / 95k | 0.100 / 7067 / 1206 / 100k | 0.096 / 3255 / 648 / 108k | 0.096 / 1414 / 338 / 121k |
| SOS MAP32 V2 | 0.084 / 14089 / 446 / 18k | 0.085 / 7067 / 262 / 21k | 0.082 / 3255 / 145 / 24k | 0.082 / 1414 / 85 / 31k |
| planisf V1 | 0.148 / 5821 / 4320 / 167k | 0.118 / 2768 / 2090 / 170k | 0.102 / 1307 / 1014 / 175k | 0.096 / 632 / 512 / 184k |
| planisf V2 | 0.116 / 5821 / 1891 / 73k | 0.103 / 2768 / 952 / 77k | 0.100 / 1307 / 483 / 83k | 0.096 / 632 / 253 / 90k |
| myhouse MAP01 V1 | 0.091 / 32743 / 213 / 8k | 0.090 / 15614 / 124 / 10k | 0.085 / 7275 / 76 / 13k | 0.087 / 3510 / 55 / 20k |
| myhouse MAP01 V2 | 0.099 / 32743 / 444 / 18k | 0.100 / 15614 / 309 / 27k | 0.112 / 7275 / 215 / 40k | 0.102 / 3510 / 165 / 61k |
| Pirates MAP54 V1 | 0.088 / 1178 / 996 / 34k | 0.082 / 602 / 519 / 35k | 0.080 / 249 / 224 / 37k | 0.078 / 118 / 112 / 38k |
| Pirates MAP49 V1 | 0.083 / 1814 / 599 / 22k | 0.080 / 875 / 286 / 22k | 0.080 / 413 / 134 / 23k | 0.080 / 204 / 76 / 27k |

- The cost is dominated by the number of indirect draws, not by triangles. Going from 64 to 256
  halves the draw count and trims up to 0.05 ms. 512 gains ≤ 0.006 ms more, but it draws 10–50 %
  more triangles, and that gets worse once fragments cost something.
- The cull dispatches themselves are flat at 0.004–0.006 ms each, even for 32k clusters.
- **Use 256.**

### GPU phases, two-phase Hi-Z, budget 256

| vantage | cull 1 | draw 1 | Hi-Z build | cull 2 | draw 2 | total | frustum-pass clusters | drawn |
|---|---|---|---|---|---|---|---|---|
| SOS MAP32 V1 | 0.005 | 0.055 | 0.030 | 0.004 | 0.002 | 0.096 | 2367 | 648 |
| planisf V1 | 0.005 | 0.060 | 0.030 | 0.004 | 0.002 | 0.102 | 1078 | 1014 |
| myhouse MAP01 V1 | 0.005 | 0.044 | 0.030 | 0.004 | 0.002 | 0.085 | 2104 | 76 |
| Pirates MAP54 V1 | 0.006 | 0.038 | 0.030 | 0.004 | 0.002 | 0.080 | 249 | 224 |
| DOOM2 MAP31 V1 | 0.005 | 0.047 | 0.030 | 0.004 | 0.002 | 0.088 | 11 | 5 |

- At a held vantage, phase 2 draws nothing because the visibility is stable. The tour A/B shows
  that phase 2 fills in revealed clusters correctly.
- Hi-Z rejects 73 % of the frustum-passing clusters on SOS V1 and 96 % on myhouse V1.
- The Hi-Z build is a constant 0.030 ms: 11 dispatches at 1280×720 with a barrier each. Draw 1
  includes the render-pass begin and the 2560×1440 clear.

### Hi-Z vs frustum-only vs no cull

`gsc` p50 (ms), budget 256 for the culled arms:

| vantage | Hi-Z | frustum | no cull | no-cull triangles |
|---|---|---|---|---|
| SOS MAP32 V1 | 0.096 | 0.100 | **0.068** | 487k |
| SOS MAP32 V2 | 0.082 | **0.065** | 0.074 | 487k |
| planisf V1 | 0.102 | 0.067 | **0.052** | 226k |
| planisf V2 | 0.100 | 0.071 | **0.059** | 226k |
| myhouse MAP01 V1 | **0.085** | 0.087 | 0.094 | 1176k |
| myhouse MAP01 V2 | 0.112 | **0.094** | 0.102 | 1176k |
| Pirates MAP54 V1 | 0.080 | 0.044 | **0.037** | 40k |
| Pirates MAP49 V1 | 0.080 | 0.044 | **0.039** | 69k |
| DOOM2 MAP31 V1 | 0.088 | 0.055 | **0.050** | 3k |

With synthetic shading cost (`r_spike_fsload 256`, `gsc` / wall p50):

| vantage | Hi-Z | frustum | no cull |
|---|---|---|---|
| SOS MAP32 V1 | 1.215 / 1.717 | 1.606 / 2.116 | **0.995 / 1.430** |
| planisf V1 | 1.109 / 1.479 | 1.102 / 1.470 | **1.047 / 1.412** |
| myhouse MAP01 V1 | **1.122 / 1.794** | 1.482 / 2.164 | 1.133 / 1.802 |
| Pirates MAP54 V1 | 0.882 / 1.247 | 0.856 / 1.210 | **0.851 / 1.199** |

**On the reference GPU, brute force (no cull) is as fast as or faster than GPU culling.** That
holds for frame cost, with and without heavy fragment work, at every stress vantage except myhouse
(a tie). The ticket asked to say so explicitly.

- A 7900 XTX draws up to 1.2 M static triangles in < 0.1 ms.
- Early-Z rejects the occluded fragments, so heavy shading costs about the same with or without
  culling.
- The fixed Hi-Z cost (0.03 ms build plus the render-pass split) is never paid back in GPU time
  here.
- Under heavy shading, GPU cost tracks the **number of indirect draws**, not the triangles.
  - On SOS V1, frustum-only issues 2367 draws, Hi-Z 648, no-cull 1. They rank 1.61, 1.22 and
    1.00 ms.
  - On myhouse V1 (frustum 2104 draws, Hi-Z 76, no-cull 1), Hi-Z and no-cull tie, and frustum-only
    is worst.
  - Every arm draws in the same cluster order, so this is per-draw overhead amplified by fragment
    load, not overdraw.
  - Ticket 08 should keep the draw count per bucket low: a larger budget, or Ironwail-style index
    compaction into one draw per bucket (research 04 §5).

**What decides the go/no-go is the CPU, not the cull.** All three arms remove the classic
scene build (`r` 0.03–0.07 ms vs 0.1–46 ms), and that is where the 15–100× comes from. Culling's
value is not frame cost on this GPU. It is:

1. **A visibility set for the CPU consumers.** Ticket 17 gathers actors from visible clusters, and
   ticket 18 writes exposure (`SSECMF_DRAWN`, `ML_MAPPED`) from it.
   - Frustum-only marks everything in the frustum. On myhouse V1 that is 2104 clusters vs 76
     really visible, a large DC5 / automap divergence.
   - It would also feed far more actors to the CPU sprite path. Sprites are the one big CPU cost
     left (SOS V1 `ss+sr` 3.1 ms).
2. **A bounded per-surface workload** for the ticket-10 shading resolve and ticket-19 light
   binning. They run only on visible surfaces, not on all 1.2 M triangles' surfaces.
3. **Weaker GPUs.** These are untested here. Helion's adaptive mode exists because brute force hurt
   iGPUs (research 04 §4).

### Moving-sector vantages

Perpetual lifts started at maptime 35 on the sectors nearest the camera:

| vantage | arm | wall p50 / p95 | r p50 / p95 | moving planes | dirty clusters | CPU z-refresh + staging (mvc) p50 / p95 / max |
|---|---|---|---|---|---|---|
| SOS MAP32 V2, 2000 lifts | classic | 25.95 / 48.97 | 21.70 / 23.49 | — | — | — |
| SOS MAP32 V2, 2000 lifts | Hi-Z (b128) | 0.861 / 1.072 | 0.141 / 0.192 | 2000 | 1796 | 0.099 / 0.134 / 0.338 |
| DOOM2 MAP27 V1, 200 lifts | classic | 0.950 / 1.084 | 0.336 / 0.385 | — | — | — |
| DOOM2 MAP27 V1, 200 lifts | Hi-Z (b128) | 0.426 / 0.506 | 0.041 / 0.056 | 188 | 54 | 0.005 / 0.006 / 0.095 |

- The CPU cost is linear in the number of moving planes: about 50 ns per plane, with no re-bake
  and no dependence on level size.
- The GPU scatter of 1796 cluster records costs 0.006 ms, the same as the static frame.
- The rest of the wall increase (0.35 ms on SOS) is the sim: 2000 plat thinkers plus 2000 plane
  interpolations, which classic pays too. Classic itself gets slower here (wall p95 49 ms) because
  the lowered floors reveal more geometry.
- **This passes research 04 avoid-3.**

**Trap found and fixed in the spike.** The first version uploaded dirty clusters with one
`VkBufferCopy` region per cluster.

- On Pirates MAP49, the map's own two moving planes belong to sectors that 508–966 clusters
  reference (huge water/floor sectors). The copy cost 0.16–0.30 ms of GPU time every frame, and the
  cost scaled with the inverse of the cluster budget.
- Replacing it with a host-visible update list plus one scatter dispatch made it 0.006 ms.
- **Ticket 09 must not use per-record copy regions**, and it should expect fan-out: one plane can
  touch hundreds of clusters.

### MSAA

The Hi-Z mip-0 pass takes the max over all depth samples (`sampler2DMS`), so it stays
conservative. With `gl_multisample 4` on SOS V1 it costs **0.261 ms** instead of 0.030 ms. Ticket
08 options:

- build mip 0 at a lower resolution;
- resolve depth with `VK_KHR_depth_stencil_resolve` (MAX mode);
- skip Hi-Z when MSAA is on.

The no-cull arm with MSAA 4× stays at 0.102 ms.

### Load time

The cluster build plus upload runs only when the path is selected (mode 0 builds nothing):

| map | triangles | clusters (b256) | build | upload |
|---|---|---|---|---|
| myhouse MAP01 | 1.18 M | 7275 | 86–90 ms | 9–25 ms |
| SOS MAP32 | 487 k | 3255 | 36 ms | 3.5 ms |
| planisf MAP01 | 226 k | 1307 | 17 ms | 1.7 ms |
| DOOM2 MAP27 | 4.5 k | 27 | 0.1 ms | 0.1 ms |

The build is linear and has not been optimized. It is on top of the `DoomLevelMesh` construction,
which the engine already does on every map load.

## Projected end-state frame cost

The spike frame is the static world only, flat-shaded. The end state adds:

- the classic sprite stages, unchanged (`ss+sr` from baseline v2);
- real shading, budgeted with the `fsload 256` delta (+0.9–1.2 ms GPU) as a pessimistic stand-in;
- the translucent pass and portals.

Projected wall p50 ≈ spike Hi-Z wall + classic `ss+sr` + 1.2 ms:

| vantage | classic wall p50 | projected | ratio |
|---|---|---|---|
| SOS MAP32 V1 | 48.4 | 0.50 + 3.11 + 1.2 ≈ 4.8 | 10 % |
| SOS MAP32 V2 | 23.0 | 0.48 + 1.64 + 1.2 ≈ 3.3 | 14 % |
| planisf V1 | 27.4 | 0.46 + 1.72 + 1.2 ≈ 3.4 | 12 % |
| Pirates MAP54 V1 | 7.75 | 0.44 + 1.44 + 1.2 ≈ 3.1 | 40 % |
| myhouse MAP01 V1 | 7.32 | 0.56 + 0.05 + 1.2 ≈ 1.8 | 25 % |

- The projection excludes portal views. Each one adds a per-view cost of roughly the levelmesh
  `gsc` (0.08–0.1 ms GPU) plus its own sprite gather. myhouse and Pirates MAP54 rely on portals,
  so ticket 15 decides their real number.
- With these additions B2 still holds with margin. Pirates MAP54 is the tightest, because sprites
  plus the shading allowance dominate a small classic frame.
- B3 (excluding sprites) is about 0.1 ms.
- B4 tails: the levelmesh frame has no level-size term, so the classic p99 spikes (scene build)
  disappear.

## Surprises

1. **Brute force wins on GPU time on the reference GPU.** See the comparison above. It moves the
   case for culling from "frame cost" to "visibility for the CPU consumers and weak GPUs".
2. **Vulkan mip sizes round down.** The first Hi-Z used ceil-halved mip sizes. The last row and
   column of every odd-sized mip fell outside the image, so writes were dropped and reads returned
   0. Hi-Z then culled visible distant geometry. The fix: floor sizes, with the last texel also
   taking the odd source texel. Ticket 08 needs a test for this.
3. **`VkImageTransition` cannot express compute.** Its shader-read layout always uses the
   fragment stage, and render passes do not update the tracked `VkTextureImage::Layout`. The spike
   writes its own barriers around the depth read (attachment → shader-read for compute → back).
   Ticket 08 should extend the helper or keep explicit barriers.
4. **Render-pass split.** `VkRenderState` has no seam for an external pipeline. The spike added
   `SpikeBeginPass()`, which begins or continues the scene pass and re-applies the
   viewport/scissor.
   - Every external draw ends the pass (`EndRenderPass()`), so the classic state is rebound
     afterwards.
   - The phase-1 / Hi-Z / phase-2 split costs two pass breaks per view. They are included in the
     numbers above.
   - The first spike pass consumes the pending scene clear.
5. **The frame is serialized.** `VkCommandBufferManager::WaitForCommands` waits for every
   submission fence at the end of each frame. GPU time therefore adds straight onto `wall`, and
   there is no CPU/GPU overlap. That is why GPU cost matters for B1 at all. It is also a later
   opportunity (frames in flight) outside this plan.
6. **ZVulkan plumbing.** `drawIndirectCount` works through the `VK_KHR_draw_indirect_count`
   extension (`VulkanDeviceBuilder::OptionalExtension` plus volk's `vkCmdDrawIndexedIndirectCountKHR`),
   with **no subtree patch**.
   - ZVulkan's `VulkanDeviceFeatures` has no Vulkan 1.1/1.2 feature structs, so the core
     `drawIndirectCount` / `shaderDrawParameters` features cannot be enabled without a patch.
   - The spike did not need `shaderDrawParameters`, because the surface id is a vertex attribute.
   - Ticket 07 should prefer the extension route, or the `flat` surface-id attribute, and keep
     ZVulkan unpatched.
7. **Thinkers spawned from the renderer vanish.** Lifts created in `DrawScene` were destroyed at
   once, presumably by the playsim's rollback/prediction state handling. Starting them in
   `P_Ticker` fixed it. This does not matter for the levelmesh, but it matters for any test
   harness.
8. **Per-region copy uploads scale badly** (moving-sector section). Fan-out from big sectors is
   real.

## Caveats

- **The image is not the classic image.** `DoomLevelMesh` lacks two-sided mid textures and draws
  whole sidedefs. It includes sky surfaces as plain geometry. It ignores portals: myhouse's
  overlapping portal spaces are all drawn. Costs are representative; pixels are not.
- Flat shading, no textures, no lights, no sprites, no translucent pass, no portal views. The
  shading allowance is synthetic.
- One GPU. No low-end or iGPU measurement was available, and research 04 §4 says brute force is
  weakest there.
- Fixed vantages keep the phase-1 visibility stable. Tours exercised phase 2 only in the
  correctness check, which was not timed.

## Recommendation: go-with-changes

1. **Go.** Removing the CPU scene build is decisive. B2 passes at 1.4–6.8 % in mesh-only frames,
   and the projection is ≤ 40 % with sprites and pessimistic shading. B3 passes at about 0.07 ms
   with no level-size term. The fixed per-view GPU cost (about 0.08 ms) fits B1's 0.10 ms slack.
2. **Cluster budget: 256.**
3. **Two-phase Hi-Z: keep it, but as the visibility source rather than a frame-cost optimization.**
   On this GPU it does not pay for itself in GPU time (no-cull is as fast or faster). Tickets 17
   and 18 still need occlusion-accurate visibility, and so does a bounded shading resolve.
   - Ticket 08 should still implement it as written, and add an MSAA policy.
   - The perf bars should not depend on Hi-Z beating no-cull.
   - Optionally, keep a no-cull draw path as a diagnostic and a low-end comparison arm.
4. **Changes to the plan.**
   - Ticket 09: scatter dirty cluster records with a compute dispatch from a host-visible list (no
     per-record copies). Budget for fan-out from big sectors.
   - Ticket 07: `VK_KHR_draw_indirect_count` through `OptionalExtension`, with no ZVulkan patch. A
     surface id as a `flat` vertex attribute avoids `shaderDrawParameters`.
   - Ticket 08:
     - keep the indirect draw count low: under shading load, GPU cost follows draw count. Measure
       index compaction (one draw per bucket) against per-cluster commands;
     - floor-rounded Hi-Z mips;
     - explicit compute barriers for depth;
     - the `VkRenderState` pass-split seam, as prototyped;
     - an MSAA Hi-Z policy (0.26 ms at 4×).
   - Map Perf bars: B1 stays as written (the measured fixed cost is about 0.08 ms < 0.10 ms).
     Record that the GPU per-view cost is serialized onto `wall` (surprise 5).
