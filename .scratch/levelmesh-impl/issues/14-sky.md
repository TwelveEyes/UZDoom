# 14: Sky

**Map:** `../map.md`: Frame model; `../research/03-portal-flow.md` (sky portals).
**Type:** task
**Status:** resolved
**Blocked by:** 10

## What to build

In the classic, sky walls and flats register sky portals (`HWSkyPortal`) that draw the dome where
the sky geometry is. In the levelmesh:

- **Mask pass.** Sky-flagged surfaces (from the bake: sky flats, sky upper walls, sky-hack walls)
  go to a **sky bucket**. They are drawn by the cull like any other surface, and the pass writes
  depth plus a stencil sky mask.
- **Dome.** The sky dome / cylinder is drawn once per sky texture configuration where the mask is
  set, reusing the classic sky renderer (`hw_skydome`, `HWSkyInfo`). Handle sky1 / sky2,
  `LEVEL_DOUBLESKY` / `LEVEL_SWAPSKIES`, the MBF sky transfer (`skytransfer`, per-sector sky
  texture: one mask value or pass per distinct sky), mist, sky fog / fadecolor rules, and
  `r_skymode`.
- **Ordering.** Match the classic's ordering relative to portals and translucent geometry.
- **Horizon and skybox portals** are ticket 15.

## Acceptance

- [x] DOOM2 MAP01 / MAP07 / MAP27 sky views, a doublesky map, and an MBF sky-transfer map: A/B
      passes.
- [x] Sky scroll is deterministic under the capture clock.

## Comments

- 2026-10-14 (from 08): Pirates MAP43 demo tick 120 (classic vs levelmesh): the levelmesh
  draws an upper wall against the sky that classic omits. Check the sky upper / sky-hack rules
  if 09 does not explain it.
- 2026-10-14 (from 09): Pirates MAP43 demo tick 120 reproduces with the state stream frozen
  (`lm_stateupdate 0`). It is not a moving sector, so check the sky upper / sky-hack rules
  here.

## Answer

**What the classic sky is.** There are no sky flats. The sky walls are `HWWall::SkyNormal` /
`SkyTop` / `SkyBottom`, running from a sky plane to z ±32768. Each visible sky configuration's
walls register an `HWSkyPortal` (deduplicated by `HWSkyInfo`), which writes them, plus caps at
z ±32767 over their bounding box, into depth and stencil, and draws the dome there.
`RenderFirstSkyPortal` draws one portal before the scene without a stencil: the dome fills the
screen, then the walls go into depth only, so geometry behind a sky wall loses to it. The rest go
through the stencil after the opaque scene.

**Bake** (`hw_levelmeshbake.cpp`, "Sky walls and caps"):
- The sky portal walls the classic emitter produces in mesh mode become `LMSF_SKY` surfaces in
  a new `LMP_SKY` bucket. In mesh mode, `HWWall::SkyPlane` leaves the portal type at -1 and
  `SkyLine` sets `PORTALTYPE_SKY`. The bucket is drawn only by the sky portals, never by the
  scene pass.
- Every sky plane gets a cap at ±32767 over its triangulation, the equivalent of the portal's
  bounding-box caps.
- Each sky surface points to a sky source: sector, plane, and the sector whose colormap fades
  it. These are the inputs `HWSkyInfo::init` takes.
- Sky-sky lines: their walls exist only while the back sector is closed (doors). The bake makes
  them for every such line, flagged `LMSF_SKYCLOSED`, and drops the copies the classic emitted
  for the doors that were closed at bake. The vertex stage shows them while the back sector's
  ceiling is at or below its floor.
- Sky clusters are separate. Sky surfaces reach ±32768, which would keep any shared cluster
  from being occluded, so they're clustered on their own in BSP leaf order. The cull always
  puts them in phase 1 (frustum test only), so the first sky's depth comes before every opaque
  bucket.
- Sides that keep their classic sky (`SideSkyOwned` 0):
  - heightsec front or back;
  - horizon lines (`DoHorizon` starts the sky at the viewer's z);
  - the Vrack2b lower-sky rule and the Icarus MAP14 door hack (both view- or offset-dependent);
  - sides with a skybox portal wall, which becomes a sky wall when the skybox can't be drawn.

**Per frame** (`HWDrawInfo::PrepareLevelMeshSky`). In the transitional state the classic walk
still decides which skies are visible, which goes first and the order. Each classic
`HWSkyPortal` becomes one sky configuration:
- Every sky source gets the configuration its current `HWSkyInfo` matches. The match is cached
  by sky textures, transfer and fade, and uploaded to the new `LMBIND_SKYSOURCES` buffer only
  when it changes.
- `HWPortal::DrawPortalStencil` skips its lines from owned sides and draws the levelmesh sky
  surfaces of its configuration instead (`DFrameBuffer::DrawLevelMeshSky`). That draw uses the
  render state's current depth / stencil / color mask, with the draw arguments of the frame's
  culls.
- The portal drawn before the scene comes before the levelmesh has culled, so it only records
  its configuration (`LevelMeshView::firstSky`). The levelmesh then draws those surfaces ahead
  of its opaque buckets in the state of the portal's depth restore (depth LEqual and written,
  alpha only).
- The classic caps are drawn only while a portal still has classic lines.
- A sky the classic didn't see this frame stays hidden in the levelmesh too. Removing that
  dependency needs visibility feedback (ticket 17/20).

**Test kit:** `make_skytest.py` (commit `12cfb9df22`): 9 rooms × 3 maps. `make_demo.py` gained
`pitch:` steps; demos need `pitch_limit` (`DEM_SETPITCHLIMIT`), without which the engine clamps
every pitch to 0.

**Results** (Vulkan, demo mode):
- **skytest**, classic vs levelmesh: MAP01 / 02 / 03 fail 0 / 1 / 0 of 173 (the one is a 4-px
  speck; run `20261017T001759Z-ab`).
  - Negative control `lm_skydraw 0`: 149 / 173 fail on MAP01.
  - Stencil path (`gl_no_skyclear 1` on both sides, `lm_sky 0` vs `1`): 4 ticks differ, a stripe
    at the cap edge in the `noskywalls` room, where both sides are black.
- **Sky scroll:** MAP02 (doublesky, both layers scrolling). Classic vs classic is identical on
  173 / 173 ticks; levelmesh vs levelmesh is identical on 172 (one 4-px speck at the bottom
  edge).
- **Whole manifest**, the sky change isolated (`lm_sky 0` vs `1`, run `20261017T014519Z-ab`):
  most ticks identical. Every tick that changes moves closer to the classic (px differing from
  the classic, `lm_sky 0` → `1`):

  | map, tick | before | after |
  |---|---|---|
  | Pirates MAP43, 120 | 7686 | 5474 |
  | Pirates MAP50, 1110 / 1200 | 226k / 167k | 196k / 137k |
  | Pirates MAP58, 9 ticks | | each 600–800 px closer |
  | SOS MAP45, 1470 | 108k | 100k |
  | myhouse MAP01, 960 | | 5k px closer |
  | DOOM2 MAP01, 2190 | | 905 px closer |

  The one exception is DOOM2 MAP01 tick 750, at 4 px.
- **Against the classic**, the fail sets equal the runs from before this ticket: DOOM2 MAP01 /
  MAP07 / MAP27, Pirates MAP43 (8 ticks, the same set), SOS MAP12 and HEXEN.
- **Test kits** (polytest, 3dfloortest, lighttest, texturetest, movers), `lm_sky 0` vs `1`: all
  identical except a 4-px speck on polytest.
- **Validation layers** on skytest (both sky paths): only two shutdown messages
  (swapchain / semaphore destruction).

**Not fixed here.** The large sky-region differences on real maps are the classic leaving
geometry out where its sky shows (ticket 23's pattern), not sky rules:
- DOOM2 MAP01 ticks 2010–2190, with the camera outside the map;
- SOS MAP12 ticks 1410–1500;
- the black blocks above the cliffs on Pirates MAP43 tick 120.

In stencil mode, the levelmesh fills in sky where the classic leaves black (Pirates MAP43 tick
840).

**Debug cvars:**
- `lm_sky 0`: classic sky walls.
- `lm_skydraw 0`: negative control.
- `lm_skylog 1/2`: per-view mapping.
- `lm_skyside N`: the bake's sky walls of a side.
- `lm_bakestats` prints `sky: N walls (N sky-sky door walls), N caps, N sources; N sides left
  classic`.
