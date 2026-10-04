# Classic-path portal & sector_link rendering flow

Type: research
Status: resolved
Blocked by:

## Question

Document exactly how the classic path renders through portals, so the levelmesh portal design has a precise
parity target:

- How `PORTAL` line specials and `sector_link` each transform geometry (coordinate space and the
  R_Project-style math in `src/rendering/hwrenderer/scene/hw_portal.cpp` and related).
- How traversal recurses into portal targets (node walks in the target sector's space? separate passes?).
- What breaks or is unsupported: fake flats through portals, sky portals vs line portals, 3D floors inside
  portal targets, recursion/re-entry limits.
- What per-frame state portals carry (view transform, clipping planes, depth handling between regions).

Output: a step-by-step rendering-flow writeup plus a checklist of every code path a levelmesh portal
implementation must replicate, with file:line references.

Findings: single Markdown file with file:line citations, per the research skill.

## Answer

Findings: `.scratch/levelmesh-rendering/research/03-portal-flow.md`

- No geometry is ever transformed or re-meshed: a portal renders the *entire level again* in a child `HWDrawInfo` whose viewpoint has been remapped (line portals: rotate+translate through origin/destination lines, `P = Dv2 + R·(P − Sv1)`, angle +mAngleDiff, alignment-based z rebase — `hw_portal.cpp:637-644`, `portal.cpp:441-522`; sector_link: pure displacement add by the per-group `mDisplacement` — `hw_portal.cpp:838-841`), then the same `RenderBSPNode` traversal runs in target space. A levelmesh design can therefore target one shared mechanism: "re-traversal with a remapped viewpoint".
- The unit of recursion is `HWDrawInfo::RenderPortal` (`hw_drawinfo.cpp:756-769`): stencil setup → child drawinfo (inherited `VPUniforms` with clip planes *reset*) → child `DrawScene(DM_PORTAL)` → child `EndDrawInfo` → stencil restore. Portals render LIFO via `portalState.EndFrame` after their own scene's opaque pass (`hw_portal.cpp:79-105`), so nested portals fall out naturally on the child's `Portals` list.
- Depth is shared between regions but explicitly cleared/restored per portal polygon (DF_Always clear + DF_LEqual restore inside increment/decrement stencil passes, `hw_portal.cpp:144-335`); the stencil is the only pixel-level region separator. Sky-ish portals skip this (no depth buffer, one may be hoisted before its scene, `hw_portal.cpp:110-123`).
- Hardest parity requirements: (1) the sector_link coverage handshake — precomputed `portalcoverage[]` (per displaced subsector, `portalgroups.cpp:282-305`), `SSRF_SEEN` marking in `SetupCoverage`, `UnclipSubsector` in the outer pass, and the `no_renderflags` node bypass (`hw_portal.cpp:766-872`, `hw_bsp.cpp:184-201, 851-863, 1044-1050`); (2) the full stencil/depth contract of §2.3 incl. cap quads and the no-stencil alpha-mark fallback; (3) per-frame portal registration with `FindPortal` de-dup and per-group merging of colinear linked portals (`hw_walls.cpp:647-744`, `portalgroups.cpp:391-465`).
- Recursion/re-entry limits: `r_portal_recursions` (default 4, `hw_cvars.cpp:45`) for line/mirror/plane-mirror; hard `renderdepth > 100` for sectorstacks (`hw_portal.cpp:829-832`); `skyboxrecursion >= 3` (`hw_portal.cpp:712-715`); `screen->instack[plane]` per-plane counters with the only read suppressing the *opposite* plane's portal while one is open (`v_video.h:141`, `hw_sky.cpp:172`); depth-buffer portals may not clear depth inside a recursion (`hw_portal.cpp:118-119`).
- Fake flats/3D floors inside targets need nothing special — the child runs the normal `hw_FakeFlat`/`render_sector` logic; the stacked sector's own plane is simply the portal plane (`hw_sky.cpp:134-176`) and 3D-floor walls are ordinary `Put3DWall` walls (`hw_walls.cpp:767-779`). One-sided portal lines in the *host* view use the destination sector as wall backsector and never add clip ranges (`hw_bsp.cpp:157-167, 404-424`); subsectors behind a portal are culled via `ClipSeg/ClipSubsector/ClipPoint` (`hw_portal.cpp:538-561`).
- Multithreading caveat: portal subsector registration is deferred to `RenderJob::PortalJob` on the worker thread because `AddSubsectorToPortal` races with wall processing (`hw_bsp.cpp:984-1016`) — a levelmesh port must preserve or explicitly replace this serialization.
