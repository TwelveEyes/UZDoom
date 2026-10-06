# 06: Portal / sector_link views + skybox

**Spec:** `.scratch/levelmesh-rendering/spec.md` §5 (portal & sector_link rendering — region
views, per-portal transforms, the locked per-frame sequence) + §7 (occlusion-query plumbing —
query pool, window-poly buffer, issue timing). Detail:
`.scratch/levelmesh-rendering/ticket-breakup/digest-05-06.md` (region partition, per-portal view
transforms, recursion, the per-frame sequence, skybox) + `digest-07-08.md` (query pool +
window-poly buffer, the predicted N+1 candidate set) + `digest-slices-proposal.md` (slice 06).

**What to build:** Region-scoped portal rendering as a first-class feature (ADR 0001 — full
portal parity, no shim). Each portal view draws its target region under a per-portal view
transform + view clip (the destination line/plane): line portals rotate + translate + angle-diff
with a z-rebase from the CURRENT sector heights (rotation/translation/angle terms derive from
the portal line pair at build time; the z-rebase is evaluated at runtime — the heights move);
sector_link is pure displacement; nested views COMPOSE (parent∘child); a mirrored composed
transform (negative determinant) draws its target region with INVERTED WINDING (frontFace CW —
the face follows the view actually drawing the region, not the region's default). Window
polygons: a small dynamic buffer, one quad per portal (two triangles), static endpoints + the
current heights of the two portal-adjacent sectors (the same heights the transforms read); each
window masks that portal's sub-range draws and its interior depth clear via the view's stencil
level. GPU occlusion queries: a depth-only window-poly draw under the main view with a depth-pass
query, one query object per portal (pool allocated at level load, sized to the portal count),
issued AFTER the main-region pass and BEFORE the portal views — for the PREDICTED N+1 candidate
set (portals inside the generous frustum of slot camera + measured velocity × dt) — gating the
NEXT frame's portal views; each query gates BOTH the portal view's draw AND its depth clear (no
clear → no spurious un-occlude); the 1-frame staleness is inherent to the design, and a portal
the prediction misses appears once it enters the candidate set and its query passes — a bounded
1–2 frame delay, visible and accepted. The LOCKED per-frame sequence (runs in the frame build,
inside the interpolation window): snapshot pass → per-portal transforms + window-poly buffer →
main-region draw (fills the depth buffer) → issue queries for the predicted candidate set (gates
the next frame) → for each portal visible per last frame's query (LIFO / registration order;
recursion with the classic caps + a per-frame VISITED-SET against cycles): stencil +1 on the
window → clear depth inside the window (depth-buffer portals only — skybox needs none) → draw
the target region's sub-ranges under the composed view transform + view clip, stencil-masked →
recurse into nested sub-views → stencil decrement + depth restore. Sprite gather per portal
view: walk that region's actor list (02) under the portal's view frustum and feed survivors into
the EXISTING sprite batch path — sprites draw inside the view's stencil/clip. The SKYBOX is an
instance of this portal-view mechanism: the sky's own transform composed in, carrying the
per-view inkybox (fog-density ×1.5) + cullcolor as per-view uniforms; ordinary flat/wall shading
inside. The per-region re-bake (04) stays per region and is shared across every view that draws
the region — no per-portal copies. A/B GL33: 20PAM (60 line portals + sector_link) FIRST as the
bring-up, then the acceptance bar myhouse MAP01 (410 line portals, 5 skyboxes, 3D floors, fake
contrast) — zero tolerance; first bring-up records + human review of diff clusters into the
committed allowlist (ADR 0004); in-game portal tour.

**Blocked by:** 04 (portal maps exercise moving sectors) + 05 (the MAP01 acceptance bar stacks
3D floors).

**Status:** ready-for-agent

- [ ] 20PAM (60 line portals + sector_link) A/B GL33 zero-tolerance as bring-up; then MAP01 (410 line portals, 5 skyboxes, 3D floors, fake contrast) A/B GL33 zero-tolerance — diff clusters recorded at first bring-up and human-reviewed into the committed allowlist (ADR 0004).
- [ ] Query protocol: pool of one query object per portal, allocated at level load and sized to the portal count; issued after the main-region pass and before the portal views, for the predicted N+1 candidate set (slot camera + measured velocity × dt, generous frustum); gates both the portal view's draw and its depth clear; no spurious un-occlude.
- [ ] Locked per-frame sequence holds: stencil +1 → depth clear inside the window → target region's sub-ranges under the composed transform + view clip, stencil-masked → recurse with the classic caps + per-frame visited set → stencil decrement + depth restore; LIFO order; no cycle hangs.
- [ ] Per-portal view transforms: line = rotate/translate/angle-diff + z-rebase from CURRENT sector heights (runtime); sector_link = pure displacement; nested views compose parent∘child; window-poly quads (static endpoints + current heights of the two portal-adjacent sectors) mask the sub-range draws + interior depth clear.
- [ ] Mirrored regions draw inverted winding — frontFace CW follows the drawing view's composed transform, not the region default.
- [ ] Sprite gather per portal view (region actor list under the portal's view frustum, into the EXISTING sprite batch path, inside the view's stencil/clip); skybox views carry the per-view inkybox (fog-density ×1.5) + cullcolor uniforms, the sky's own transform composed in.
- [ ] cvar 0 = classic, unchanged; in-game portal tour; the bounded 1–2 frame appearance delay on fast camera turns is visible and accepted per spec.

**Note:**
1. The acceptance bar is myhouse MAP01 (user decision 2026-10-06): 20PAM (60 line portals + sector_link) goes green first as the bring-up — MAP01 (410 line portals, 5 skyboxes, 3D floors, fake contrast) is the bar, and it needs 05 because it stacks 3D floors.
2. The spec's §5 vs §7 disagree on the query-issue position; follow §5: the queries are issued after the main-region pass and BEFORE the portal views — they must test against the main region's depth, not portal-cleared depth.
