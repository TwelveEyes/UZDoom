# 15: Portals, mirrors, skyboxes, horizons (multi-view)

**Map:** `../map.md`: Frame model, Clusters ("spatial only; portal topology separate"),
`../research/03-portal-flow.md`.
**Type:** task
**Status:** resolved
**Blocked by:** 12, 14

## What to build

Portal rendering at full classic semantics, on top of multi-view GPU culling:

- **Views.** Each portal type becomes a view with its own transform, clip plane (`mClipLine` /
  `mClipHeight`, written as clip distances), and stencil level, as the classic `HWPortal` state
  machine does. The types are: line portals, mirrors (inverted winding), stacked sector portals,
  `sector_link`, skybox viewpoints, and horizons.
- **Culling per view.**
  - Frustum per view, with the portal window narrowing the frustum where cheap.
  - Hi-Z per view only if ticket 05 / 08 data says it pays; otherwise frustum-only for portal
    views.
  - Each view has its own persistent visibility bitset.
- **Portal discovery without the BSP walk.** Portal window surfaces come from the bake's side list
  as their own bucket.
  - Candidate portals: CPU frustum test of window quads (cheap, exact, same frame).
  - Gating: the main view's cull writes a per-portal visible flag (any window surface passing Hi-Z /
    depth). Portal views are recorded for all candidates and gated with
    `VK_EXT_conditional_rendering` on that flag.
  - Fallback where the extension is missing: draw every candidate, or use the previous frame's flag
    (DC3).
- **Recursion.** Classic caps (`gl_portals` depth, count) plus a visited set. Window stencil +1,
  depth clear inside the window, nested views, then restore.
- **Skybox / horizon.** Same mechanism with the skybox transform. Apply the inkybox fog boost and
  cullcolor in the resolve pass's per-view block.
- **Ownership.** Remove portal walls from classic ownership. During the transitional period, the
  classic portal path is replaced for levelmesh levels.

## Acceptance

- [x] myhouse 20PAM (60 line portals + sector_link) A/B passes; then myhouse MAP01 (410 line
      portals, skyboxes, 3D floors): only DC1 / DC2 (DC3 only on the no-extension fallback).
- [x] A mirror map passes (winding, clip).
- [x] Per-view GPU timings on the portal-dense vantage logged against the ticket-02 classic numbers.

## Comments

- 2026-10-14 (from 08): `VkLevelMeshRenderer` is main view only.
  - `Draw` refuses any target whose depth is not the scene depth.
  - Visibility is one `uint` per cluster in a single buffer, and there is one Hi-Z image. Both
    need to become per view.
  - Back-face culling is a pipeline bit (front face counter-clockwise, `lm_backfacecull`).
    Mirrors need the flipped front face from the render state's cull mode.
  - The pipeline cache key already includes the render-pass and state bits.

## Answer

**Transitional, like ticket 14.** The classic portal state machine still finds the portals,
sets up each view (transform, clip line / height, stencil, recursion caps, `instack`,
`skyboxrecursion`) and draws the stencil, the caps and everything the levelmesh doesn't own.
Inside each portal view, the levelmesh now draws the scene in place of the classic lists, the
way it does for the main view. GPU portal discovery, conditional rendering and the visited set
need the views known before the scene (no BSP walk), so they move to ticket 20, along with the
perf point below.

**What changed:**
- `HWDrawInfo::DrawLevelMesh` also runs for child drawinfos (`outer != nullptr`). It sends
  `LevelMeshView::portal`, and `mirrored` from `portalState.isMirrored()`. The classic lists
  then drop what the levelmesh owns, as in the main view. The vertex shader already had the
  clip line / height planes.
- `VkLevelMeshRenderer` keeps per-view buffers (visibility, counters, draw arguments, drawn
  clusters, cull set): the main view's, plus up to 64 portal views per frame, created on
  demand per level. A portal view is frustum-culled in one phase (no Hi-Z), resolved and
  drawn with per-cluster commands. The main view's Hi-Z, visibility and draw arguments (still
  used by the sky portals afterwards) stay untouched.
- Mirrors: the pipeline key has a mirrored bit, which flips the front face to clockwise.
- Once-per-frame setup (`BeginFrameSetup`: harvest, Hi-Z / level resources, the timing slot)
  is run by whichever levelmesh view comes first, so a portal drawn before the main scene
  works too.
- **Window rectangle:** `HWDrawInfo::PortalCullRect` projects the portal's stencil geometry
  (its lines, plus the caps at ±32767 when `DrawPortalStencil` draws them) with the outer view.
  The cull narrows its clip space to that rectangle, so its frustum test is against the window.
  The full screen is used when any corner is behind the eye, or for a sector stack in an
  out-of-body view (its stencil is drawn from flats).
- **Portal views drawn without a stencil** (`RenderFirstSkyPortal`'s) stay classic. They're
  bounded only by what the classic draws in them (sector-stack coverage, `SetupCoverage`), so
  the levelmesh would draw the whole frustum there. On myhouse MAP01 it showed a house's gable
  roof where the classic leaves sky: 21 failing ticks, 0 with this rule.
- Skybox fog: the shading block already carries `inskybox` (`LMSH_INSKYBOX`).

**Test kit:** `make_portaltest.py`, 12 rooms × 2 maps (plain, fog); see the abtest README.
Writing it turned up two classic behaviours:
- A one-sided portal line with floor / ceiling alignment is never drawn: `HWWall::Process`
  returns from its alignment switch. The test room uses a two-sided line.
- The Eternity horizon portal (`Sector_SetPortal` 4) is created by `PutPortal` but never
  queued, so it shows black.

**Results** (Vulkan, demo mode; A = portal views classic, `lm_portals 0`):
- **portaltest**, A against the default: MAP01 / MAP02 fail 4 / 1 of 217. They are specks, plus
  a 199-px delta-5 band in the plane-mirror room (run `20261017T130234Z-ab`). The negative
  control `lm_portaldraw 0` fails 115 of 217.
- **portaltest**, classic against levelmesh: 59 / 41 failing ticks of scattered main-view
  ceiling / floor specks (≤ ~750 px, delta ≤ 10). They're the same with portal views classic,
  so they predate this ticket.
- **Whole manifest**, A against the default (run `20261017T132051Z-ab`): everything passes
  except a 4-px speck on DOOM2 MAP27 and Pirates MAP43.
  - On Pirates MAP43 (3 ticks, up to 3,913 px), the water's reflection now shows the dark blocks
    above the cliffs that the levelmesh main view already has (ticket 23's pattern, mirrored).
- **myhouse 20PAM**, classic against levelmesh: 0 of 87 fail.
- **myhouse MAP01**, classic against levelmesh: 5 of 87 fail, the same set with portal views
  classic (ticket 24's).
- **Validation layers** on portaltest: only the two shutdown messages.

**Per-view GPU timings** (perflog v7 `lmpv` / `lmpgpu`; vantages, p50):

| vantage | portals | levelmesh portal views | their GPU ms | main view GPU ms | wall: classic / A / levelmesh |
|---|---|---|---|---|---|
| myhouse 20PAM V1 | 3 | 3 | 0.150 | 0.249 | 1.064 / 1.190 / 1.058 |
| myhouse MAP01 V1 | 7 | 2 | 0.178 | 0.250 | 6.951 / 7.780 / 7.505 |
| myhouse MAP01 V2 | 8 | 2 | 0.152 | 0.268 | 6.970 / 7.38–7.70 / 8.21–8.87 |

- Before the window rectangle and the stencil-less rule, MAP01 V1's two views cost 0.93 ms.
  Most of that was the full-screen sector stack drawn before the scene.
- MAP01 V2 is 0.8 ms slower with levelmesh portal views, even though their GPU work is 0.15 ms.
  The cause is the render-pass break: each view ends the scene render pass for its compute cull,
  and the classic path restarts it. An experiment where each portal view only ended the render
  pass gave `wall` p50 8.67 / 8.88 ms (`fin` 0.85 against 0.51).
- The fix is to cull all portal views together before the scene's render pass, which needs the
  views known up front (ticket 20's GPU portal discovery). The perf bars apply after ticket 20.

**Debug cvars:**
- `lm_portals 0`: portal views classic.
- `lm_portaldraw 0`: negative control.
- `lm_portalrect 0`: whole-screen cull.
- `lm_portallog 1`: print each portal view the levelmesh draws.
