# Portal & sector_link rendering in the levelmesh world

Type: grilling
Status: resolved
Blocked by: 03, 04

## Question

Lock how the levelmesh path renders through portals at full parity (classic-path behavior documented in
ticket 03's findings):

- Region model: how the level partitions into renderable regions — one mesh per portal-connected component,
  per sector_link target, or something else?
- Per-portal view-transform handling on the GPU (target-region transform in the shader/descriptor state vs
  separate draws).
- Fake flats and sky through portals.
- 3D floors inside portal targets.
- Recursion/re-entry limits and draw order/batching across regions.

End state: a spec-ready section with the region model and the per-frame portal render sequence.

## Answer

**Rationale.** Region-scoped portal views. The level is statically partitioned by portal structure at build
time; each portal view draws only its target region, bounding per-portal vertex cost and keeping per-frame
CPU off level size (the classic path re-traverses the whole level per portal — `research/03-portal-flow.md`).
Helion's flood-fill fake-wall approach (01) was rejected as the parity path by ADR 0001.

**Region model.**
- **Partition:** one global flood-fill over the sector graph; adjacency = two sectors sharing a two-sided line
  that is *not* a portal (portal lines are barriers; one-sided lines never connect). Each connected component
  is one region.
- **Main region** = the component containing the player's start; the rest are **portal regions**.
- A portal's **target** = the region behind its *destination* line (line portals) / the destination sector
  (sector_link), fixed at build time from the destination line + the portal transform.
- The **main view** draws only the main region; **each portal view** draws its target region plus nested
  sub-views. The portal line's window wall belongs to the main region (it is the visible frame in the main
  view).
- **Fake flats / 3D floors / sector_link destinations** need no special mechanism — they are extra surfaces
  referencing the model sector (04) and sit in the subsector's region; the region-scoped view covers them.
- **Re-bake** stays per region (04) and is shared across every view that draws that region — no per-portal
  copies.

**Per-portal view transform.**
- A portal view is an instance of one shared mechanism: a per-portal view transform + view clip applied to
  the target region's per-texture IBO sub-ranges (04) — not per-portal mesh baking.
- Line portals: rotate + translate + angle + z-rebase from *current* sector heights, evaluated at runtime
  (heights move); sector_link: pure displacement.
- Nested portals compose transforms (parent ∘ child).
- **Sky / skybox** are instances of the same portal-view mechanism; this ticket locks the mechanism, ticket 07
  owns sky texturing/effects.
- Classic parity: `DoInterpolations` writes tick-blended heights before render (05), so the portal's
  current-height sampling matches the classic per-frame behaviour exactly.

**Visibility (GPU occlusion query).**
- Per-portal depth-only draw of the window polygon (under the main view) with a depth-pass query; result > 0
  = visible.
- **1-frame-ahead prefetch:** the queries issued in frame N (after the main pass, tested against the main
  region's depth) gate frame N+1's portal draws. The result is inherently one frame stale.
- **Candidate set = predicted set:** frame N+1's camera = current camera + measured camera velocity × dt;
  the candidate set is the portals inside the predicted frustum (generous margin / widened FOV).
- **Misses:** a portal the prediction misses (fast camera move) is drawn once it enters the candidate set and
  its query passes — a bounded 1–2 frame delay, accepted.
- The query gates both the portal draw *and* its depth clear (an invisible portal gets no depth clear → no
  spurious un-occlude).

**Recursion / re-entry.**
- Classic caps preserved (max portal nesting depth, max portals per frame) plus a per-frame visited-set to
  break cycles.
- Draw order: main pass first, then portals in LIFO / registration order; a nested portal draws inside its
  parent's view (stencil level +1, composed transform).

**Per-frame portal render sequence.**
1. `DoInterpolations` → snapshot pass (05).
2. Per-portal transforms (line: rotate/translate/angle + z-rebase from current heights; sector_link:
   displacement).
3. Draw the main region (all main-region × texture IBO sub-ranges, main view, z-buffered) — fills the depth
   buffer.
4. Issue occlusion queries for the predicted candidate set (depth-only window-polygon draw, main view) —
   gates the *next* frame.
5. For each portal visible per last frame's query (LIFO / registration order; recursion + visited-set):
   stencil increment on the polygon → clear depth inside (depth-buffer portals only; sky/skybox need none) →
   draw the target region's sub-ranges under the portal view transform (composed with any parent) + view clip
   (destination line/plane), stencil-masked → recurse nested sub-views → stencil decrement + depth restore.
6. Sprites (08), z-buffered against the levelmesh.

**Implementation notes (08's territory).** The query mechanism is backend-specific (Vulkan occlusion query
pool vs GL33 depth-pass query object). The predicted frustum is generous to cover fast camera moves; the
velocity is the measured per-frame camera delta.
