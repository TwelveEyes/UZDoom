# 05: Spike: GPU-driven culling go/no-go

**Map:** `../map.md`: Transitional strategy (the go/no-go gate), Frame model.
**Type:** prototype
**Status:** resolved
**Blocked by:** 02, 04

## Question

On the ticket-02 stress vantages, does GPU-driven cluster culling plus indirect drawing of the
static world beat the classic path by enough to justify tickets 06–21?

## What to build (throwaway, on a spike branch; do not merge)

- **Geometry.** Take the classic `DoomLevelMesh` geometry (positions + elements + surface ids). Cut
  it into spatial clusters by BSP subtree, with a triangle budget, and build 3D AABBs from the
  load-time heights.
- **Vulkan compute.** Frustum cull plus two-phase Hi-Z (phase 1: last frame's visible clusters;
  phase 2: the rest against the new Hi-Z). Write `vkCmdDrawIndexedIndirectCount` commands.
- **Shading.** A minimal shader: per-surface flat color, or one bindless texture lookup if
  descriptor indexing is easy to enable. Lighting does not matter here; cost does.
- **Mesh-only mode.** A cvar that skips `CreateScene` entirely and draws only the spike mesh, so the
  scene-build cost is removed.
- **Measurements.** At each stress vantage, compare against classic (Vulkan):
  - CPU render window;
  - GPU time per phase (timestamp queries: cull 1, draw 1, Hi-Z, cull 2, draw 2);
  - triangles drawn;
  - cluster counts.

  Sweep the cluster budget (64 / 128 / 256 / 512 triangles). Also report frustum-only, to show
  what Hi-Z buys.
- **No-cull arm (Helion-style brute force; `../research/04-prior-art-vkdoom.md` §4).** Draw the
  whole spike mesh every frame with no culling, one indirect command per bucket. If this beats
  cull + draw at the stress vantages on the test GPU, say so: it changes the go/no-go.
- **Moving-sector vantage (research 04 §1.4, §6 avoid-3).** Add a vantage or demo segment with many
  moving planes (lifts, doors, crushers). Geometry stays at load heights in the spike. Each frame,
  refresh the z-range of the clusters those sectors touch and upload the dirty cluster records.
  Report that CPU + upload cost; it must stay flat with no per-frame re-bake.

## Deliverable

`../research/05-spike-results.md` with tables, plus a **go / no-go / go-with-changes**
recommendation against the ticket-02 bars, including:

- the projected end-state frame cost;
- the cluster budget to use;
- whether two-phase Hi-Z is needed;
- any surprises (MSAA depth for Hi-Z, render-pass splits in `VkRenderState`, ZVulkan feature
  plumbing).

The user decides. A no-go stops tickets 06+ for a re-plan.

## Acceptance

- [x] Numbers for every stress vantage and the four cluster budgets, plus the no-cull arm and the
  moving-sector vantage.
- [x] Clear recommendation; user decision recorded in `../map.md` Decisions log.
- [x] Tickets 06–21 updated (triaged to `ready-for-agent`, or changed) according to the findings.

## Comments

- 2026-10-13 (resolved): results in `../research/05-spike-results.md`. The spike code is on branch
  `spike-05-gpucull` (throwaway, never merged).
  - Mesh-only frames (no `CreateScene`, no BSP walk), measured against classic Vulkan:
    - wall p95 is 1.4–6.8 % of classic on the seven heavy vantages (B2 passes);
    - render CPU p95 is 0.05–0.07 ms everywhere (B3 passes);
    - the fixed GPU cost per view is about 0.08 ms (fits B1's 0.10 ms slack).
  - The projected end state, with classic sprites and a pessimistic shading allowance, is
    10–40 % of classic wall.
  - No-cull (Helion-style) is as fast as or faster than GPU culling in GPU time on the 7900 XTX,
    with or without synthetic shading load. Culling's value is the visibility set for tickets
    10/17/18 and for weak GPUs, not frame cost here.
  - Under shading load, GPU cost follows the number of indirect draws.
  - Moving sectors: 2000 lifts cost 0.1 ms CPU (linear in moving planes) plus a 0.006 ms GPU
    scatter. No re-bake.
  - Trap: per-record copy regions cost 0.16–0.30 ms of GPU time when big sectors fan out to
    hundreds of clusters. A compute scatter fixes it.
  - Other surprises:
    - Vulkan mips round down, which broke the first Hi-Z;
    - `VkImageTransition` has no compute stage;
    - the frame is CPU/GPU serialized (`WaitForCommands`);
    - MSAA 4× Hi-Z build costs 0.26 ms;
    - `VK_KHR_draw_indirect_count` works without a ZVulkan patch.
  - **User decision (2026-10-13):** go-with-changes. Two-phase Hi-Z is kept as the visibility
    source (the bars do not assume it beats no-cull). Cluster budget 256. The `--set` option of
    `run_matrix.sh` / `contact_sheet.py` was ported to the main branch.
  - Tickets 06–21 were triaged to `ready-for-agent`, and 07 / 08 / 09 / 10 / 17 were amended with
    the findings.
