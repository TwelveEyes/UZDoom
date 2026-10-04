# Research 03: Classic-path portal & sector_link rendering flow

Type: research — ticket `.scratch/levelmesh-rendering/issues/03-portal-flow.md`
Primary source: local UZDoom tree (read-only). All citations are `file:line`.

Goal: document exactly how the classic (BSP/HW) renderer draws through `PORTAL` line
specials (`PORTT_VISUAL`/`PORTT_LINKED`) and sector_link/stacked-sector portal planes
(`PORTS_STACKEDSECTORTHING`, `PORTS_PORTAL`, `PORTS_LINKEDPORTAL`), so a levelmesh portal
implementation has a precise parity target.

## 1. Data model & per-frame portal registration

### 1.1 Portal data structures
- `FLinePortal` (line-to-line portal record: origin line, destination line, displacement,
  type, alignment `PORG_ABSOLUTE/PORG_FLOOR/PORG_CEILING`, sin/cos rotation, group):
  `src/playsim/portal.h:204-224` (struct body approx 205-224).
- `FLinePortalSpan` — merged span of colinear `PORTT_LINKED` portals (v1/v2 endpoints +
  `lines` array + `validcount`): `src/playsim/portal.h:227-236`.
- `FSectorPortal` — one sector plane portal: `mType` (`PORTS_SKYVIEWPOINT`,
  `PORTS_STACKEDSECTORTHING`, `PORTS_PORTAL`, `PORTS_LINKEDPORTAL`, `PORTS_PLANE`,
  `PORTS_HORIZON`), `mPlane`, `mOrigin`/`mDestination` sectors, `mDisplacement`,
  `mPlaneZ`, `mSkybox` actor: `src/playsim/portal.h:240-291` (enums 242-251, flags 254-257,
  struct 258-291).
- `FSectorPortalGroup` — groups sector portals with identical `mDisplacement` + plane so
  the renderer traverses the target space once per group: `src/playsim/portal.h:293-298`.
- Sector-side lookup: `sector_t::portals[2]` (`FSectorPortalGroup*`),
  `GetPortalGroup(plane)`: `src/gamedata/r_defs.h:1153`; `ClearPortal`: `r_defs.h:1103-1107`.
- `line_t::portaltransferred` (index into `Level->sectorPortals` once a linked portal's
  sector side is resolved at runtime): `src/gamedata/r_defs.h:1539`; sector side accessor
  `portaltransferred`: `src/g_levellocals.h:890`. Maploader wires special 443/`sector_link`
  via `line.portaltransferred = pnum`: `src/maploader/specials.cpp:336-343`.

### 1.2 Grouping (once per map / after saves, not per frame)
- `InitPortalGroups(Level)` → `GroupSectorPortals` + `GroupLinePortals`:
  `src/r_data/portalgroups.cpp:468-473`.
- `CollectPortalSectors`: only `PORTS_STACKEDSECTORTHING / PORTS_PORTAL / PORTS_LINKEDPORTAL`
  are "offset-displacing" and get groups; keyed by displacement:
  `src/r_data/portalgroups.cpp:310-323`.
- `GroupSectorPortals`: one group per (displacement, plane); fills each origin sector
  `sec->portals[plane]` and precomputes per-subsector **portal coverage**
  (`sub->portalcoverage[plane]` = list of target-space subsectors whose displaced
  footprint intersects the origin subsector) via `BuildPortalCoverage`:
  `src/r_data/portalgroups.cpp:328-386`.
- `BuildPortalCoverage` intersects the (displaced) subsector shape against the level BSP:
  `src/r_data/portalgroups.cpp:282-305`.
- `GroupLinePortals`: merges colinear connected `PORTT_LINKED` portals into
  `Level->linePortalSpans`, sets `linePortals[i].mGroup`:
  `src/r_data/portalgroups.cpp:391-465`.
- Call sites: map load `src/maploader/maploader.cpp:3242`, after load games
  `src/p_saveg.cpp:1048`. So groups are static per level; the renderer re-creates
  per-viewpoint `HWPortal` objects every frame.

### 1.3 Per-frame HWPortal creation (during wall processing)
`HWWall::PutPortal` is the single dispatch that converts a rendered wall into a portal
object, de-duplicated by source via `HWDrawInfo::FindPortal` (reverse linear search,
`hw_drawinfo.cpp:573-581`) and pushed into `di->Portals` (a per-drawinfo stack):
`src/rendering/hwrenderer/scene/hw_walls.cpp:647-744`:
- `PORTALTYPE_HORIZON` → `HWHorizonPortal` (`hw_walls.cpp:660-669`)
- `PORTALTYPE_SKYBOX` → `HWSkyboxPortal` or `HWEEHorizonPortal` if type != `PORTS_SKYVIEWPOINT` (`hw_walls.cpp:671-681`)
- `PORTALTYPE_SECTORSTACK` → `HWSectorStackPortal` (keyed on the `FSectorPortalGroup`) (`hw_walls.cpp:683-691`)
- `PORTALTYPE_PLANEMIRROR` → `HWPlaneMirrorPortal` (unique by plane pointer) (`hw_walls.cpp:693-703`)
- `PORTALTYPE_MIRROR` → `HWMirrorPortal` (keyed on `line_t*`) + envmap surface (`hw_walls.cpp:705-718`)
- `PORTALTYPE_LINETOLINE` → `HWLineToLinePortal` (keyed on `FLinePortalSpan*`); on first
  creation also calls `ddi->ProcessActorsInPortal(otherside->getPortal()->mGroup, ...)` to
  enqueue destination-side actors for sprite culling (`hw_walls.cpp:720-735`)
- `PORTALTYPE_SKY` → `HWSkyPortal` (unique by `HWSkyInfo`) (`hw_walls.cpp:737-743`)
- `portal->planesused |= (1 << plane)` records which plane(s) host the portal
  (`hw_walls.cpp:747-749`); `boundingBox` is grown per line in `HWPortal::AddLine`
  (`hw_portal.h:110-115`).

### 1.4 Which wall kinds produce which `ptype` (PutPortal call sites)
- **Line portals (PORTAL line special):** decided by
  `isportal = seg->linedef->isVisualPortal() && seg->sidedef == seg->linedef->sidedef[0]`
  (`hw_walls.cpp:2265`). One-sided visual portal line: `PutPortal(PORTALTYPE_LINETOLINE)`
  with `ztop/zbottom` taken from the front sector's ceiling/floor
  (`hw_walls.cpp:2268-2281`). Two-sided visual portal line: portal plus the mid
  texture are both drawn (`hw_walls.cpp:2462-2476`).
- **Sky-position planes** (`HWWall::SkyPlane`, `hw_sky.cpp:134-200`):
  - regular `skyflatnum` (or skybox disabled) → `PORTALTYPE_SKY` (`hw_sky.cpp:144-151`);
  - `PORTS_STACKEDSECTORTHING/PORTS_PORTAL/PORTS_LINKEDPORTAL` → `PORTALTYPE_SECTORSTACK`,
    with oob/ortho/viewpoint-beyond-plane early-outs (`hw_sky.cpp:160-166`), group lookup
    (`hw_sky.cpp:168`), `PortalBlocksView` skip (`hw_sky.cpp:171`) and the opposite-plane
    re-entry guard `screen->instack[1 - plane]` (`hw_sky.cpp:172`);
  - `PORTS_SKYVIEWPOINT/PORTS_HORIZON/PORTS_PLANE` → `PORTALTYPE_SKYBOX` (`hw_sky.cpp:179-183`);
  - reflective plane → `PORTALTYPE_PLANEMIRROR` (`hw_sky.cpp:196`).
- **Two-sided lines between two portal-group sectors:** the mid/sky handling suppresses
  the portal wall when front and back resolve to the same group or the back side blocks
  view: ceiling `hw_sky.cpp:338-341`, floor `hw_sky.cpp:421-424`.
- **Horizon:** `PutPortal(PORTALTYPE_HORIZON)` in the sky-mid code
  (`hw_walls.cpp:1035, 1064, 1071`). **Mirror:** `hw_walls.cpp:1372`.
- 3D-floor walls themselves never become portals — they are ordinary walls built by
  `HWWall::Put3DWall` → `PutWall` (`hw_walls.cpp:767-779`, called at 949/970/980); only
  the *replaced* sky/sector plane of a stacked sector becomes a `PORTALTYPE_SECTORSTACK`.

## 2. Frame flow (both portal kinds share it)

### 2.1 Outer scene
- `HWDrawInfo::ProcessScene` → `DrawScene(DM_MAINVIEW)`: `hw_drawinfo.cpp:978-990`.
- `DrawScene`: `CreateScene()` (BSP traversal that fills drawlists AND `di->Portals`),
  then `portalState.RenderFirstSkyPortal()` (may draw a sky/skybox portal *before* the
  main scene), then `RenderScene()` (opaque → masked → models → decals passes,
  `hw_drawinfo.cpp:800-846`), then `portalState.EndFrame()` (renders all queued portals),
  then `RenderTranslucent()`: `hw_drawinfo.cpp:928-975`.
- `portalState.StartFrame()` increments `renderdepth` and at depth 0 resets
  `inskybox` and `screen->instack[floor/ceiling]`: `hw_portal.cpp:53-60`.
- `portalState.EndFrame()` pops `di->Portals` LIFO; for each portal with `lines.Size() > 0`
  calls `RenderPortal(p, state, true, di)` then `delete p`; decrements `renderdepth`:
  `hw_portal.cpp:79-105`.
- `FPortalSceneState::RenderPortal` (the per-portal wrapper): temporarily swaps the
  projection matrix to `ProjectionMatrix2` for sky/skybox portals in ortho views, then
  `outer_di->RenderPortal(...)`: `hw_portal.cpp:126-137`.

### 2.2 The one recursive unit: `HWDrawInfo::RenderPortal`
`src/rendering/hwrenderer/scene/hw_drawinfo.cpp:756-769`:
1. `gp->SetupStencil(this, state, usestencil)` — write stencil/depth for the portal
   polygon in the *outer* drawinfo (see §2.3).
2. `StartDrawInfo(Level, this, Viewpoint, &VPUniforms)` — new child `HWDrawInfo` for the
   portal target. Note `StartScene` with a `uniforms` argument **copies VPUniforms from
   the parent but resets clip line/height** (`mClipLine.X = -1000001`, `mClipHeight = 0`)
   because "The clip planes will never be inherited from the parent drawinfo":
   `hw_drawinfo.cpp:116-125`. Clippers (`mClipper/vClipper/rClipper`) are singletons
   re-pointed per scene: `hw_drawinfo.cpp:108-120`.
3. `new_di->mCurrentPortal = gp` (mirror-side queries, attached-actor rendering,
   dither-transparency suppression).
4. `gp->DrawContents(new_di, state)` → for scene portals `HWScenePortalBase::DrawContents`
   = `Setup(di, state, clipper)`; if true: `di->DrawScene(DM_PORTAL)`; `Shutdown(di, state)`;
   else `state.ClearScreen()`: `hw_portal.h:167-189`.
5. `new_di->EndDrawInfo()` (drawinfo returned to pool; also runs nothing else — the
   child's `DrawScene` already rendered its own nested portals via its own `EndFrame`).
6. Re-bind the *outer* vertex buffer and outer viewpoint, then
   `gp->RemoveStencil(this, state, usestencil)`.

Recursion depth is therefore: outer `DrawScene` → `EndFrame` → `RenderPortal` → child
`DrawScene` → child `EndFrame` → …, one level per `renderdepth`.

### 2.3 Stencil & depth between regions
`HWPortal::SetupStencil` (`hw_portal.cpp:185-250`):
- with `usestencil` (always true for the EndFrame path, `hw_portal.cpp:96`):
  increment stencil inside the portal polygon (`SOP_Increment`, color mask off,
  `EFF_STENCIL`), `screen->stencilValue++`; then for `NeedDepthBuffer()` portals an extra
  `DF_Always`/depth-range-(1,1) pass **clears the depth buffer inside the stencil**, then
  restores normal draw state. For `!NeedDepthBuffer()` portals (skies) a single
  all-in-one stencil draw with depth write on, then depth test off.
  Mirror portals translate the view by `zshift * GetMirrorSide()` before/after to place
  the stencil on the mirrored plane.
- without stencil (`usestencil == false`, e.g. first-sky-portal path): just disable
  depth write/test if no depth buffer needed.
- Saves `savedvisibility` from the viewpoint camera's `RF_MAYBEINVISIBLE` renderflags.
`HWPortal::RemoveStencil` (`hw_portal.cpp:255-335`):
- Restores camera renderflags.
- With stencil: `STP_DepthClear` (far depth, `DF_Always`) to reset depth inside the
  polygon, then `STP_DepthRestore` with `SOP_Decrement` + `DF_LEqual` to restore the
  previous depth values and pop the stencil; `stencilValue--`; stencil ops back to `SOP_Keep`.
- Without stencil: `Clear(CT_Depth)` (depth-buffer portals) or re-enable depth; then a
  `STP_DepthRestore` pass with `STYLE_Source` writing only the alpha channel so level
  geometry can't clobber the portal's depth.
`HWPortal::DrawPortalStencil` (`hw_portal.cpp:144-180`): builds `mPrimIndices`
(vertex-index + vertex-count per portal line, triangle fans) plus optional top/bottom
**cap quads** (±32767 z-extent over `boundingBox`) when `NeedCap()`; caps get
`SetDepthRange(1,1)` treatment during `STP_DepthRestore` so they don't block other caps.

`NeedDepthBuffer`/`NeedCap`/`IsSky` defaults: base `true/true/false`
(`hw_portal.h:141-147`); scene portals override `NeedDepthBuffer()==true`
(`hw_portal.h:162`); line portals `NeedCap()==false` (`hw_portal.h:195`);
sky/skybox/horizon/sectorstack `IsSky()==true` with `NeedDepthBuffer()==false`
(`hw_portal.h:227-230`, `hw_portal.h:257`, `hw_portal.h:336-339`, `hw_portal.h:357-360`).

**Parity point:** the outer scene and every portal target render into the *same* FBO;
depth is shared but is explicitly cleared/restored per portal polygon, and the stencil
is the only thing that keeps regions from overwriting each other's screen pixels.

### 2.4 Sky portals before the scene
`FPortalSceneState::RenderFirstSkyPortal` (`hw_portal.cpp:110-123`): picks the
visible sky-ish portal with the most lines (or the one containing the viewpoint, when
stencilling) and renders it *before* the main scene with `usestencil =
outer_di->Viewpoint.bDoOob` (usually false → no stencil, depth test off). A portal
needing a depth buffer is skipped inside recursion (`if (recursion && p->NeedDepthBuffer()) continue`).

## 3. PORTAL line special (line-to-line) — coordinate space

`HWLineToLinePortal::Setup` (`hw_portal.cpp:620-668`):
- Recursion gate: `renderdepth > r_portal_recursions` → refuse (returns false →
  `ClearScreen` in `DrawContents`). GATE-LINE: `hw_portal.cpp:628`.
- Viewpoint remap through the **origin** line (`glport->lines[0]->mOrigin`), in this order:
  `P_TranslatePortalXY` for `vp.Pos`, `vp.ActorPos`, `vp.Path[0]`, `vp.Path[1]`, `vp.OffPos`;
  `P_TranslatePortalAngle` for `vp.Angles.Yaw`; `P_TranslatePortalZ` for `vp.Pos.Z` and
  `vp.OffPos.Z`: `hw_portal.cpp:637-644`.
- Self-visibility fix-up: if the view path crosses the destination line and the camera
  would sit inside the target, set `RF_MAYBEINVISIBLE` on the viewpoint camera so the
  player doesn't render a copy of themselves through their own portal
  (`hw_portal.cpp:645-655`).
- Map-section unlock for each destination sector (POLYOBJ lines use
  `PointInRenderSubsector(v1)` instead of `frontsector->subsectors[0]`):
  `hw_portal.cpp:657-665`.
- `di->SetClipLine(glport->lines[0]->mDestination)` — the **destination line** becomes
  the view-plane clip for everything drawn in the target: `hw_portal.cpp:667`.
- `di->SetupView(...)` rebuilds view/projection from the transformed viewpoint and
  registers it in the viewpoint buffer: `hw_portal.cpp:668` →
  `HWDrawInfo::SetupView` `hw_drawinfo.cpp:541-547` (view matrix assembly with mirror
  signs in `SetViewMatrix`, `hw_drawinfo.cpp:519-530`).
- `ClearClipper` re-initializes the 2D angle clipper from the portal line angles
  (shared by line/skybox/sectorstack portals, `hw_portal.cpp:596-617`).

The transform math itself (`src/playsim/portal.cpp`):
- `P_TranslatePortalXY` (`portal.cpp:441-462`): offset viewpoint by the origin line's
  v1, rotate by the portal's precomputed `mCosRot/mSinRot` (rotation matching the
  destination line's orientation; `PORTF_POLYOBJ` portals refresh rotation via
  `SetPortalRotation` first), then translate to `mDestination->v2`. i.e.
  `P = Dv2 + R·(P − Sv1)`.
- `P_TranslatePortalAngle` (`portal.cpp:488-495`): `angle += mAngleDiff`.
- `P_TranslatePortalZ` (`portal.cpp:503-522`): for `PORG_FLOOR`/`PORG_CEILING` aligned
  portals, re-base z on the height difference between the origin line's frontsector
  floor/ceiling at v1 and the destination frontsector's at v2 (runtime, "cannot be
  precalculated because heights may change"). `PORG_ABSOLUTE` passes z through.
- Clipping a line against the portal: `P_ClipLineToPortal` (`hw_portal.cpp:538-546`
  wrapper `HWLinePortal::ClipSeg`; full test in `portal.cpp`, used also by
  `ClipSubsector` (`hw_portal.cpp:548-554`, all segs on the wrong side → `PClip_InFront`)
  and `ClipPoint` (`hw_portal.cpp:556-561`). These are invoked from
  `HWDrawInfo::AddLine` (`hw_bsp.cpp:315-318`), `RenderThings`/`RenderParticles`
  (`hw_bsp.cpp:725-727, 744-746`) and `DoSubsector` (`hw_bsp.cpp:868-876`).

**How traversal "recurses":** it does not walk a second BSP in a special way — the
child `HWDrawInfo` re-runs the *same* `RenderBSPNode` on `Level->HeadNode()`
(`hw_drawinfo.cpp:762-763` → `CreateScene` → `RenderBSP`, `hw_bsp.cpp:1089-1136`) with
the **transformed** `Viewpoint.Pos`/`OffPos`, so the BSP side tests and clippers operate
in destination space. The child's `DrawScene(DM_PORTAL)` again ends in
`portalState.EndFrame`, so portals visible *inside* the target are queued on the child's
`Portals` list and rendered recursively under the same `renderdepth` counter
(`hw_drawinfo.cpp:928-975`, `hw_portal.cpp:79-105`).

### 3.1 One-sided portal lines in the *outer* view
`RenderJob::WallJob` special case: a one-sided portal line uses the portal's
destination sector as its backsector for wall construction (with a slope-out rule:
any sloped plane → no backsector): `hw_bsp.cpp:157-167`.
Also `AddLine` skips the "same sector midtexture invalid → return" fast path for
`isVisualPortal()` lines and never adds a clip range for them when front==back sector
(`hw_bsp.cpp:404-424`). Subsectors completely behind a line portal are culled by
`mClipPortal->ClipSubsector`, with a fallback that still draws lines lying exactly on
the portal boundary (`AddSpecialPortalLines`, `hw_bsp.cpp:868-876`, `hw_bsp.cpp:598-618`).
Actors "attached" to the portal back side: `HWLineToLinePortal::RenderAttached` →
`di->ProcessActorsInPortal(glport, ...)` at the end of every `RenderBSP`
(`hw_portal.cpp:670-673`, `hw_bsp.cpp:1144`).

## 4. sector_link / stacked-sector (sectorstack) portals — coordinate space

`HWSectorStackPortal::Setup` (`hw_portal.cpp:826-874`):
- Emergency recursion gate `renderdepth > 100` (`hw_portal.cpp:829-832`) — a *separate*
  hard cap from `r_portal_recursions`.
- Viewpoint shift: `vp.Pos += origin->mDisplacement`, same for `ActorPos`, `OffPos`;
  `ViewActor = nullptr` (`hw_portal.cpp:838-841`). No rotation, no z math — pure
  translation by the group's displacement.
- `screen->instack[origin->plane]++` guards **re-entry into the same stacked plane**
  (`hw_portal.cpp:844`), decremented in `Shutdown` (`hw_portal.cpp:893-896`).
  `screen->instack` ("globally maintained state for portal recursion avoidance",
  `v_video.h:141`) is read in exactly one place: `hw_sky.cpp:172`, where a
  stacked-plane portal on the *opposite* plane (`1 - plane`) is suppressed while a
  portal for this plane is open, so a floor+ceiling stacked pair at the same
  displacement renders exactly one portal per frame. Plane-mirror portals swap the
  two counters while active (`hw_portal.cpp:929, 1025`).
- When the viewpoint is out-of-bounds-capable (`bDoOob`) and the portal has a polygon,
  a z clip plane is set on the portal plane: `di->SetClipHeight(planeZ, ±1)`
  (`hw_portal.cpp:845-850`).
- `di->SetupView(...)`, then `SetupCoverage` marks every target-space subsector whose
  displaced shape overlaps a portal origin subsector as `SSRF_SEEN` and caches the
  result in `no_renderflags[]` for BSP nodes: `hw_portal.cpp:766-823`
  (`SetCoverage` 769-785, `SetupCoverage` 787-823).
- Coverage semantics on the *outer* side: in `DoSubsector`, a subsector flagged
  `SSRF_SEEN` triggers `UnclipSubsector` — the outer view clears the clipper range the
  portal covers and lets the portal's own pass draw it: `hw_bsp.cpp:851-863`,
  `hw_bsp.cpp:184-201`. In `RenderBSPNode`, nodes not passing `mClipper->CheckBox` are
  still descended if `no_renderflags[node] & SSRF_SEEN`: `hw_bsp.cpp:1044-1050`.
  If the (transformed) viewpoint itself is not inside any covered subsector, the
  clipper is invalidated entirely and "blocked" until portal subsectors re-open ranges:
  `hw_portal.cpp:864-872`, `hw_bsp.cpp:851-856`.
- `HWSectorStackPortal::DrawPortalStencil` override: when `vpIsAllowedOoB` the stencil
  is drawn as the actual flat vertex data of the origin subsectors (per-plane
  `flat.iboindex` + normals) instead of triangle fans over the lines:
  `hw_portal.cpp:877-891`.

Registration into groups happens while flats are processed: at the end of
`DoSubsector`, if the (fake) sector has a portal group on ceiling/floor,
`AddSubsectorToPortal(portal, sub)` is queued (as `RenderJob::PortalJob` for the
worker thread, or directly): `hw_bsp.cpp:983-1016`. `AddSubsectorToPortal`
finds-or-creates the `HWSectorStackPortal` on `di->Portals` keyed by the
`FSectorPortalGroup` and records the subsector:
`hw_drawinfo.cpp:1087-1101`. The wall-side registration path
(`PORTALTYPE_SECTORSTACK` in `HWWall::PutPortal`, `hw_walls.cpp:683-691`) is used
for the wall parts of stacked sectors (3D floors — GAP-A covers which wall kinds).

## 5. Fake flats, sky, 3D floors through portals

- `hw_FakeFlat(sector, in_area, back)` produces the per-subsector fake sector whose
  floor/ceiling planes come from the heightsec (3D floor) stack state; it is the
  function used in every worker job and in `DoSubsector`: `hw_bsp.cpp:144-220,
  866, 1042`. `sub->render_sector` + `SSRF_SEEN`/`SSRF_PROCESSED` machinery handles
  "the planes of this subsector are faked to belong to another sector" — i.e. 3D
  floors: `hw_bsp.cpp:1028-1040`. The stacked sector's *own* plane (where the
  `skyflatnum`/linked portal sits) is not drawn at all — it is replaced by the
  `PORTALTYPE_SECTORSTACK` portal created in `SkyPlane` (`hw_sky.cpp:134-176`), and the
  3D-floor walls of the heightsec stack are ordinary `Put3DWall` walls
  (`hw_walls.cpp:767-779`).
- Sky: sky portals are regular `HWPortal`s created in `HWWall::PutPortal`
  (`PORTALTYPE_SKY`, `hw_walls.cpp:737-743`); sky through a line portal works because
  the child scene is a full re-traversal — sky walls in the target space produce their
  own `HWSkyPortal`/horizon on the child's `Portals` list and are rendered after the
  child's scene. `RenderFirstSkyPortal` can additionally hoist *one* sky to render
  before its scene (§2.4). Skyboxes: `HWSkyboxPortal::Setup`
  (`hw_portal.cpp:706-744`) places the viewpoint at the skyviewpoint actor's
  interpolated position (clamped 4 MU off floor/ceiling), scales fog by 16 (skybox
  sectors are 16× scaled), disables depth clamp, increments its own `skyboxrecursion`
  (hard cap 3, `hw_portal.cpp:712-715`), sets `inskybox` and
  `PORTSF_INSKYBOX` on the origin portal to block re-entry. `IsSky()==true` lets
  `RenderFirstSkyPortal` hoist it (`hw_portal.h:227-230`).
- `HWEEHorizonPortal::DrawContents` (Eternity-style sector portal that renders up to
  sky + two horizon planes instead of a BSP re-traversal):
  `hw_portal.cpp:1014-1060`.
- 3D floors *inside* a portal target: nothing special — the child traversal runs the
  normal `hw_FakeFlat`/`render_sector` logic in destination space. The only coupling
  is the plane `instack` counter (§4) which exists because stacked-sector (sector_link)
  portals are *also* the stacked-sector/3D-floor rendering mechanism in the same view.
  GATE: subsectors of a sector with an active heightsec are skipped for flat drawing
  when `in_area == area_default` ("Exclude the case when it tries to render a sector
  with a heightsec but undetermined heightsec state", `hw_bsp.cpp:1019-1022`).

## 6. Recursion / re-entry limits (consolidated)

| Limit | Value | Where checked | What it protects |
|---|---|---|---|
| `renderdepth > r_portal_recursions` | cvar, **default 4** (`hw_cvars.cpp:45`) | `HWLineToLinePortal::Setup` `hw_portal.cpp:628`; `HWMirrorPortal::Setup` `hw_portal.cpp:518`; `HWPlaneMirrorPortal::Setup` `hw_portal.cpp:924` | nested scene portals |
| `renderdepth > 100` | hard | `HWSectorStackPortal::Setup` `hw_portal.cpp:829-832` | sectorstack cycles |
| `skyboxrecursion >= 3` | hard | `HWSkyboxPortal::Setup` `hw_portal.cpp:712-715` | skybox-in-skybox |
| `screen->instack[plane]` | per-plane count | `HWSectorStackPortal::Setup/Shutdown` `hw_portal.cpp:844, 893-896`; reset in `StartFrame` `hw_portal.cpp:55-58` | same stacked plane re-entered (also drives fake-flat under-plane suppression, GAP-B) |
| `PORTSF_INSKYBOX` flag | | set/cleared around skybox `Setup/Shutdown` `hw_portal.cpp:718, 753` | skybox self-visibility |
| depth-buffer clear in recursion | | `RenderFirstSkyPortal` skips `NeedDepthBuffer` portals when `recursion` is non-zero `hw_portal.cpp:118-119` | "Cannot clear the depth buffer inside a portal recursion" |

## 7. Per-frame portal state carried between regions

- View transform: copied `VPUniforms` (projection + view + clip vars) from outer to
  child via `StartDrawInfo`/`StartScene`, with clip line/height **reset**
  (`hw_drawinfo.cpp:116-125`); child then overwrites the view via its portal's
  `SetupView` (§3/§4). Mirror portals additionally translate the view by
  `zshift * GetMirrorSide()` around the stencil draws (`hw_portal.cpp:197-200, 240-243,
  279-282, 320-323`).
- 2D angular clipper: re-derived from scratch per portal by `ClearClipper`
  (`hw_portal.cpp:596-617`); line portals additionally `SetClipLine(destination)`
  (`hw_portal.cpp:667`), sectorstacks `SetClipHeight` under oob (`hw_portal.cpp:845-850`).
- `SSRF_SEEN` coverage: `ss_renderflags`/`no_renderflags`/`section_renderflags`
  arrays, cleared per scene in `ClearBuffers` (`hw_drawinfo.cpp:213-231`), written by
  portal `SetupCoverage` (§4) and consumed by the outer traversal (§4).
- Depth: shared depth buffer, cleared + restored per portal polygon via stencil passes
  (§2.3). `screen->stencilValue` tracks the current stencil level.
- `di->mClipPortal` (line-portal clip tests during traversal, §3) and
  `di->mCurrentPortal` (mirror side, attached actors, set in `RenderPortal`,
  `hw_drawinfo.cpp:758`), both cleared in `ClearBuffers` (`hw_drawinfo.cpp:235-236`).
- Fullbright flags propagate outer→inner (`hw_drawinfo.cpp:146-147`).
- Map sections: each portal `Setup` re-asserts `CurrentMapSections` for its target
  (`hw_portal.cpp:580-584, 657-665, 789-798, 901-907`).

## 8. Checklist: code paths a levelmesh portal implementation must replicate

1. **Per-frame portal registration** — walls/flats that belong to a portal must create
   one shared `HWPortal` object per (source) per drawinfo (`FindPortal` de-dupe,
   `hw_drawinfo.cpp:573-581`; `PutPortal` switch, `hw_walls.cpp:647-744`), accumulate
   its line set + `boundingBox` + `planesused` (`hw_portal.h:110-115, 747-749`), and
   keep it in a per-drawinfo `Portals` stack. (Call sites per wall kind: §1.4.)
2. **LIFO deferred rendering** — portals render only after their own scene's opaque
   pass, via `EndFrame` pop + `delete`, with nested portals on the child's list
   (`hw_portal.cpp:79-105`, `hw_drawinfo.cpp:928-975`).
3. **Stencil contract** — increment/decrement stencil on the portal polygon with
   `DF_Always` depth clear + `DF_LEqual` restore passes, cap quads, and the
   no-stencil alpha-mark fallback (`hw_portal.cpp:144-180, 185-250, 255-335`).
4. **Child drawinfo per portal** — fresh clipper set, inherited-then-reset
   `VPUniforms` clip planes, `mCurrentPortal` linkage
   (`hw_drawinfo.cpp:108-147, 756-769`).
5. **Viewpoint transform in the *source* portal's math** — for line portals:
   rotate+translate through origin/destination lines + angle diff + alignment-based z
   rebase + view-path self-visibility flag (`hw_portal.cpp:637-655`,
   `portal.cpp:441-522`); for sector_link: pure displacement add of
   `mDisplacement` per group/plane (`hw_portal.cpp:838-841`).
6. **Full BSP re-traversal of the target in child space** — same `RenderBSPNode`
   with transformed view (`hw_bsp.cpp:1019-1077`, `hw_drawinfo.cpp:762-763`); no
   dedicated "target-space walk" exists.
7. **Portal-plane clipping of child geometry** — `ClipSeg/ClipSubsector/ClipPoint`
   against the portal lines (`hw_portal.cpp:538-561`) used in `AddLine`,
   `RenderThings`, `RenderParticles`, `DoSubsector` (`hw_bsp.cpp:315-318, 725-727,
   744-746, 868-876`), plus `SetClipLine(destination)` / `SetClipHeight` for the view
   plane.
8. **Coverage/unclip handshake** (sector_link only) — precomputed
   `portalcoverage[]` (`portalgroups.cpp:282-305`), `SSRF_SEEN` marking +
   `UnclipSubsector` in the outer pass, `no_renderflags` node bypass in
   `RenderBSPNode`, blocked-clipper-when-viewpoint-outside handling
   (`hw_portal.cpp:766-872`, `hw_bsp.cpp:184-201, 851-863, 1044-1050`).
9. **instack plane counters** for stacked planes (re-entry + fake-flat suppression)
   (`hw_portal.cpp:844, 893-896`; GAP-B read sites).
10. **One-sided portal lines in the host view** — destination sector as backsector
    for wall construction, no clip-range addition for visual portals, boundary-line
    fallback drawing, attached-actor pass
    (`hw_bsp.cpp:157-167, 404-424, 598-618, 1144`, `hw_portal.cpp:670-673`).
11. **Sky/skybox hoisting** — `RenderFirstSkyPortal` pre-scene render, no-stencil +
    depth-test-off path, recursion skip for depth-buffer portals, 16× skybox scaling
    and fog compensation, `skyboxrecursion` cap 3
    (`hw_portal.cpp:110-123, 706-758`).
12. **All recursion gates** — §6 table, plus depth-clear prohibition in recursion
    (`hw_portal.cpp:118-119`).
13. **Recursion-safe per-frame state** — `renderdepth`, `inskybox`, `MirrorFlag`,
    `PlaneMirrorFlag/Mode`, unique-sky/horizon lists reset in `BeginScene`/`StartFrame`
    (`hw_portal.h:149-164`, `hw_portal.cpp:53-60`).
14. **Multithreading caveat** — portal subsector registration is deferred to
    `RenderJob::PortalJob` in the worker; the comment explicitly warns
    `AddSubsectorToPortal` can't run in the main thread under `gl_multithread`
    (`hw_bsp.cpp:984-1016`, `hw_bsp.cpp:212-214`). A levelmesh port that changes
    worker job types must preserve this.

## Resolved gaps
- GAP-A (PutPortal call sites per wall kind): §1.4.
- GAP-B (`instack` read sites): only `hw_sky.cpp:172` (opposite-plane suppression);
  declaration `v_video.h:141`; swap in plane mirrors `hw_portal.cpp:929, 1025`.
- GAP-C (3D floors inside portal targets): ordinary `Put3DWall` walls in the child
  traversal; the stacked sector's own plane is the portal (sky-plane path,
  `hw_sky.cpp:134-176`); `sub->render_sector` plane faking `hw_bsp.cpp:1028-1040`.
- GAP-D: `r_portal_recursions` default 4 (`hw_cvars.cpp:45`).

## Notes / caveats
- Line numbers were verified against grep anchors where the full-file read and grep
  disagreed (e.g. `SkyPlane` starts at `hw_sky.cpp:134`, not 160).
- The `HWSectorStackPortal::SetupCoverage`/`SetCoverage` code (`hw_portal.cpp:766-823`)
  marks `SSRF_SEEN` on *target-space* subsectors using the precomputed
  `portalcoverage[]`; the outer scene then unclips those ranges (`hw_bsp.cpp:184-201,
  851-863`) — this is the key "two passes, one screen" handshake for sector_link.
