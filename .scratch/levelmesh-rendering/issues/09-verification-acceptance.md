# Verification & acceptance plan

Type: grilling
Status: resolved
Blocked by: 08 (resolved), 11 (resolved)

## Question

Lock the acceptance plan (direction locked at charting: deterministic A/B rendering tooling is in scope):

- Acceptance map list: name concrete maps/WADs covering vanilla, complex ports, portals/sector_link, 3D
  floors, and slaughter scale.
- The A/B tool design: how it drives the engine (same map, same camera, both paths), what it captures,
  pixel-diff tolerances, where it lives in the tree, how it runs headless or in-game.
- Profiling targets: the frame-time budget the levelmesh path must meet or beat (the classic-path baseline
  is produced by ticket 10).
- The precise definition of "proven" that gates flipping the default to levelmesh.
- The in-game soak checklist.

End state: a spec-ready verification section.

## Locked decisions (grilling, 3 rounds, 2026-10-04)

R1:
1. **Capture mechanism**: an always-compiled engine **A/B capture mode** (cvar-gated, default off) + an
   external driver. Verified constraint: no existing path is tick-precise — `I_GetTimeFrac()` is wall-clock
   even under `-timedemo`/`cl_capfps` (`DoInterpolations` at d_main.cpp:585, sky scroll via `R_UpdateSky`,
   `TexAnim`, camera FOV interpolation all take wall-clock time), so determinism is built in-engine, not
   emulated from the driver.
2. **WADs**: `SOS_Boom` (the slaughter-scale map) + **the full set** (DOOM II, Hexen, Heretic, classic
   ports — TNT/Ritual/Rampage/Spectre class). `doom1.wad` (shareware) is the vanilla baseline regardless.
   **Amended 2026-10-04 (user)**: the classic-ports row and `doom1.wad` are dropped from the matrix;
   the acceptance set is the WADs actually present in `tools/abtest/wads/`.
3. **Tolerance**: **zero tolerance outside a frozen per-map known-diff allowlist** — recorded at first
   bring-up, human-reviewed (only z-fighting clusters are allowlisted; real bugs get fixed), committed to
   the repo, frozen (any NEW diff fails), shrink-only.
4. **Camera coverage**: **both** static-spawn captures (full matrix) **and** demo replay (subset of maps;
   demos recorded once with the classic path, input replay is path-independent).

R2:
5. **WAD reality**: the full set is confirmed available — canonical portal/sector_link/skybox/fog/
   fake-contrast coverage from real WADs; no hand-rolled test WAD is required.
6. **Tool home**: `tools/abtest.py` (Python 3, Pillow + stdlib) + a committed manifest + committed
   allowlists under `tools/abtest/`. Runs on a GPU dev machine with a display; **not a CI job** (no
   headless mode exists — GL33 needs a display context, Vulkan a GPU; CI runners are build-only).
7. **Frame scope**: the **final composed framebuffer** (post-2D — 3D world + HUD + overlays), the buffer
   `M_ScreenShot`'s PNG writer already reads. The HUD is identical code on both paths: free parity
   coverage, one capture point.
8. **Backends**: **both GL33 and Vulkan must pass**. Parity is per-backend (same-backend pairs only):
   classic-GL33 vs levelmesh-GL33 and classic-VK vs levelmesh-VK — 4 runs per map+mode.

R3:
9. **Perf protocol**: median + p95 of **three** metrics — total frame time, CPU render window, GPU 3D
   window. Fixed manifest: 1920×1080, vsync off, gamma 1.0, default visual cvars. 10 s warmup discarded,
   60 s window, demo-driven camera. Reference hardware = the dev machine, identity recorded in the spec
   (the bar is relative — same machine, both paths). Measured by a small per-frame perf-log hook
   (cvar → file), built on the existing `FrameCycles` timing, the `RenderView` window, and the
   levelmesh worker fence wait (ticket 08).
10. **Perf bar**: (1) **no-regression floor** — levelmesh median frame time ≤ classic × 1.05 on every
    acceptance map, both backends; (2) **scale payoff** — `SOS_Boom`: levelmesh ≤ 50% of classic median
    frame time.
11. **PROVEN** = all four: A/B pass (every map × mode × both backends, zero tolerance outside the frozen
    allowlist) ∧ perf pass (decision 10) ∧ soak pass (the checklist below) ∧ no open levelmesh-only bug
    of severity ≥ S2 (S1 = crash/hang/memory growth; S2 = visible artifact or broken behavior on an
    acceptance map under normal play; S3 = cosmetic/rare). The implementer runs the tool and posts
    results; the map owner declares proven; the `gl_uselevelmesh` default flip ships with that
    declaration (ticket 08 locked default-false at handoff).
12. **Soak**: the full list (below), ~half a day, human-driven.

## Levelmesh spec: verification & acceptance

### Acceptance map list

WADs (availability user-confirmed 2026-10-04; classic-ports row + doom1 dropped 2026-10-04, user
decision) and the map sets:

- **doom2.wad**: MAP01 (baseline), MAP07 (3D floors + lift), MAP21 (killfloor scale + 3D floors), MAP23
  (light thinkers), MAP27 (fog), MAP30 (3D floors + scale), MAP31 (dense scale).
- **hexen.wad**: MAP01, MAP10, MAP27 (pinned 2026-10-04, phase 1) covering vanilla BEHA-format
  specials (teleport lines 80, lifts, sector special bits). **No category pins** — scanner-verified:
  zero line portals, sector_links, 3D floors, skyboxes, fog, fake contrast. (Original category lock
  re-categorized 2026-10-04, user decision.) Note: vanilla Hexen DOES carry 170 polyobject lines
  (special 1 = Polyobj_StartLine, playsim/actionspecials.h:25) across all 31 maps — format coverage,
  not a category pin.
- **heretic.wad**: E1M2, E5M6 (pinned 2026-10-04, phase 1), vanilla DOOM-format coverage
  (door/lift density, sector specials). **No feature pins** — scanner-verified: zero features.
- **myhouse.pk3** (user-provided; UDMF, ns=zdoom): **MAP01** — portals (508, incl. 98×
  Sector_SetPortal), 3D floors (1065), polyobjects (1693), plane equations (960), smoothlighting,
  fake contrast (4804 nofakecontrast sides), **skybox** (5 TID-less SkyViewpoints + 50 SkyPickers);
  **20PAM** — portal pairs (60×156), **sector_link (7×107)**, fake contrast, 3D floors. Covers
  four of the five original hexen categories in one archive.
- **Pirates!.wad** (BEHA): **MAP50/51/57** — `fogdensity` + 3D floors; MAP43/49/54/58 —
  polyobjects + 3D floors (MAP50/58 also carry SkyViewpoints). Covers the remaining category (fog).
- **SOS_Boom.wad** (Summer of Slaughter by TH1RT3EN; 36 maps, DOOM format + xlat, UMAPINFO):
  **MAP32** — slaughter scale (61,623 lines / 9,907 sectors / 76,458 vertices); MAP12 (19,529),
  MAP45 (19,005), MAP46 (14,659) as secondary scale entries. The scale payoff bar (decision 10)
  applies to MAP32. No feature pins — scanner-verified: zero portals/link/skybox/fog/fake-contrast;
  UMAPINFO carries only sky textures (MAP04/46), music, and map flow (incl. MAP15→43 secret exit).
- **planisf2.wad**: single map, 37,265 lines / 6,287 sectors / 32,162 vertices; DOOM format + xlat,
  no MAPINFO. Scale + geometry coverage, no feature pins.

#### WAD inventory — scanner-verified (2026-10-04)

Scanner: `tools/abtest/scan_maps.py` (source-verified model: BEHA gating + xlat DOOM-format numbers,
UDMF 0-based args, UDMF namespace semantics — raw number space only for zdoom/dsda/eternity/vavoom/
hexen, everything else incl. boom/mbf/missing is xlat-translated (udmf.cpp:2492-2560), dual
special-number label space (BEHA vs xlat names), 8-char full lump names, ENDMAP terminators,
phantom-marker rejection, duplicate-map first-wins flagging, PK3/nested-WAD mounting, UMAPINFO/
ZMAPINFO with engine per-WAD priority MAPINFO>ZMAPINFO>UMAPINFO (g_mapinfo.cpp:2764), `skytexture`
UMAPINFO sky key (umapinfo.cpp:215 → SkyPic1)). Feature columns: `port` (57/156/301),
`link` (107/51), `3df`, `poly`, `plane` (full floor/ceiling plane equations), `sky` (skybox), `smth`
(smoothlighting), `fakec` (nofakecontrast sides, UDMF-only).

Verified signal for `sky` (UZDoom `wadsrc/static/zscript/actors/shared/skies.zs` + `SpawnSkybox` in
`maploader/specials.cpp`): SkyViewpoint thing (9080; ZDoom 4434) **with no TID becomes the default 3D
skybox**; SkyPicker (9081/4435) references one by TID; EE-style skybox = SkyCamCompat (9082/4436) + a
`Sector_SetPortal` (57) line with `arg1==2` in its sector. Binary skybox requires BEHA layout (the xlat
never maps 57).

| WAD | maps | layout | features verified |
| --- | --- | --- | --- |
| DOOM2.WAD | 32 | doom14 | none (vanilla, as expected) |
| HEXEN.WAD | 31 | beha16 | **zero** portals/link/3df/sky/fog/fakec… classic specials (80× teleports) + **170 polyobject lines** (special 1, all maps) — polyobj is format coverage, not a category pin; MAPINFO has only classic sky1/2/3 designators — **cannot satisfy the hexen category locks** |
| HERETIC.WAD | 48 (27+21 dev) | doom14 | none |
| STRIFE1.WAD | 34 (22+12 dev) | doom14 (no BEHA → DOOM format + xlat) | none |
| SOS_Boom.wad (user-provided) | 36 | doom14 (UMAPINFO, 36 sections) | **none of the five** (zero portals/link/sky/fog/fakec); slaughter scale: **MAP32 = 61,623 lines / 9,907 sectors / 76,458 verts** (MAP12 19,529, MAP45 19,005, MAP46 14,659 lines); UMAPINFO: `skytexture` MAP04/46, music, secret flow (MAP15→43); 4-digit specials (12184–25688, packed `K*1024+off`) are custom — outside the xlat table → **zeroed on load** |
| planisf2.wad (user-provided) | 1 | doom14 | none; 37,265 lines / 6,287 sectors / 32,162 verts |
| Pirates!.wad | 19 | beha16 | 3D floors near all maps (605 lines), polyobjects, **fogdensity MAP50/51/57**, **skybox viewpoints MAP50×3 + MAP58×2** |
| myhouse.pk3 (user-provided; 14 nested WADs) | 2 live blocks, UDMF ns=zdoom | udmf | **MAP01**: 165,922 lines; 508 portals (incl. 98×57), 1065×3D floors, 1693×polyobjects, 960 plane equations, 1884×Plane_Align, 146 smoothlighting, 4804 nofakecontrast sides, **skybox: 5 SkyViewpoints (all TID-less → default skybox) + 50 SkyPickers**; **20PAM**: 60×156 portals, **7×sector_link(107)**, 42 nofakecontrast sides, 7×3D floors. MAPINFO uses `adddefaultmap` + HUSTR names (MAP02–MAP30 defined but absent from the archive — dead entries). |

**Lock resolution (2026-10-04, user: re-categorize):** of the five original hexen category locks,
four (portals, sector_link, skybox, fake contrast) are pinned onto **myhouse.pk3** (MAP01 + 20PAM);
**fog** is pinned onto **Pirates! MAP50/51/57**. Vanilla hexen.wad carries none of the five by
construction and stays in the matrix as BEHA-format coverage with no feature pins (same for
heretic.wad). The classic-ports row (TNT/Ritual/Rampage/Spectre) and doom1.wad were then dropped
from the matrix entirely (2026-10-04, user); the acceptance set is the WADs in
`tools/abtest/wads/`.

### A/B tool: engine capture mode

Always-compiled, cvar-gated (name spec detail, e.g. `r_ab_capture <ticks> <outdir>`), default off. When
active:

1. **Synthetic clock**: exactly one tick per rendered frame, real-time paced (`I_WaitForTic`-style), so
   every tick gets exactly one frame and the frame after tick N shows post-N state. All time-derived
   values are computed from it, not the wall clock: the interpolation fraction pinned at 0.5
   (`DoInterpolations`), sky scroll (`levelSkyPos` inputs — `hw_sky1/2/mistpos`, ticket 07), texture
   animation (`TexAnim`), camera FOV interpolation.
2. **Capture**: after final composition (post-2D — the final framebuffer), the ticks in the list are
   written as PNGs named `<map>_<tick>.png` to the output dir, reusing the existing `M_ScreenShot` PNG
   writer.
3. Demos: the mode also accepts `-playdemo` input; demo input is consumed per tick in the sim, so replay
   is identical under synthetic pacing for both paths.

### A/B tool: driver, manifest, run structure

- `tools/abtest.py` (Python 3, Pillow + stdlib) + `tools/abtest/manifest.*` (map, WADs, tick lists, mode,
  resolution) + committed allowlists under `tools/abtest/allowlists/`.
- **Run structure**: per map × mode × backend: two engine launches (classic, levelmesh), each a **fresh
  level load** (ticket 11: exposure and dither state are monotonic per load — a single session cannot
  host both paths). Same-backend pairing. Config manifest applied per run via a temp autoexec: the
  resolution, `vid_preferbackend`, `gl_uselevelmesh`, gamma 1.0, vsync off, `vid_maxfps 0`, `cl_capfps`
  off, default visual cvars, fixed language/UI scale, screenshot notification silenced.
- **Modes**: *static* — no input; the per-map tick list must cover the first ~200 tics (spawn state,
  thinker init), a tick at a light-thinker/animated-flat frame boundary, and a tick with a door open or
  lift mid-transit (concrete ticks per map pinned in the manifest at bring-up). *demo* — a recorded demo
  (one per map, few seconds, recorded once with the classic path) replayed on both paths; captures at
  fixed ticks (e.g. every 30 tics).
- **Resolutions**: 1920×1080 (head), 320×200 clean (subset of maps), 2560×1080 stress-only (subset).
- **Cvar sweep** (ticket 07's stress list): on the stress maps, gamma {1.0, 0.9} × glow on/off, on the
  fog/fake-contrast/animated-light/scroll/skybox maps already in the matrix.
- **Comparator**: per-pixel diff of the PNG pair; connected components of the diff bitmap are the
  clusters; any pixel outside the allowlist fails the run.

### Tolerance & known-diff allowlist

Zero tolerance outside the allowlist. First bring-up: the tool records every diff cluster per
map+mode(+resolution); a human reviews each — real bug → fix, z-fight (ticket 04: true-z vs the
classic's normalized per-view depth range — either winner legitimate) → allowlist entry. The allowlist
is committed JSON (map, mode, resolution, tick, cluster rect/centroid+radius, reason), frozen: any new
diff fails; entries can only be removed (re-review). The z-fighting budget from ticket 04 is thus
enforced exactly as allowed, nothing else.

### Profiling: protocol & bar

- **Protocol** (decision 9): per map+backend — median + p95 of total frame time, CPU render window,
  GPU 3D window; 1920×1080, vsync off, gamma 1.0, default visual cvars; 10 s warmup discarded, 60 s
  window, demo-driven camera (same demos as the A/B matrix); reference machine recorded in the spec.
- **Measurement hook**: a small per-frame perf-log cvar (name spec detail) appending `frame <n> total
  <ms> cpu_render <ms> gpu3d <ms>` to a file. Classic: `cpu_render` = the `RenderView` window
  (traversal + batch + issue), `gpu3d` = the classic fence wait. Levelmesh: `cpu_render` = the levelmesh
  frame-build window (cull walk + uploads + 2D), `gpu3d` = the worker fence wait (ticket 08). Total =
  the existing `FrameCycles` timing in `D_Display`.
- **Bar** (decision 10): (1) every acceptance map: levelmesh median total frame time ≤ classic × 1.05,
  both backends; (2) SOS_Boom: levelmesh ≤ 50% of classic median.
- **Buffer streaming budget** (fog from the map): the per-frame upload is formula-bound —
  `sectorCount × 96 B` (sector state ring, ticket 05) + the per-level 3D light state (12 B/light,
  ticket 07) + the `levelSkyPos` triple; vertex uploads settle once per level (ticket 04, height
  sub-uploads only while planes are unstable). At 50k sectors that is ~4.8 MB/frame (≈288 MB/s H2D at
  60 fps) — inside any realistic budget; ticket 10 measures the actual numbers on the real WADs.
- Ticket 10 (classic baseline, HITL) runs this exact protocol; its deliverable is the baseline table
  the levelmesh is compared against.

### PROVEN gate

PROVEN = A/B pass ∧ perf pass ∧ soak pass ∧ no open levelmesh-only bug ≥ S2 (severities per decision
11). The implementer runs `tools/abtest.py` (full matrix) + the perf protocol, posts the results; the
map owner declares proven; the `gl_uselevelmesh` default flip + SettingsPage entry (ticket 08) ship
with that declaration.

### Soak checklist (human-driven, levelmesh on, ~half a day)

1. Full DOOM II campaign, Normal.
2. Full Hexen campaign (vanilla BEHA coverage; the feature categories are carried by the myhouse
   + Pirates! maps, items 4–5).
3. Heretic E1.
4. myhouse MAP01 + 20PAM tours, 10+ min each (portals + 3D floors + sector_link + skybox).
5. SOS_Boom MAP32: 15-min fast-camera fly (scale + sector-state upload budget); portal load is
   carried by myhouse MAP01 in the matrix (SOS_Boom has no portals).
6. Scripted save/load + automap fog + secret-gating walk — ticket 11's acceptance items (flag array /
   savegame diff, automap screenshot A/B, secret gating, dither + decay + cap-20, cullcolor,
   save/restore round trip, radar/ortho behavior).
7. 15-min deathmatch on a portal map (dither + OOB views + exposure under stress).
8. Light-thinker / fog / fake-contrast tour (myhouse MAP01 + DOOM2 MAP27 + an animated-light map),
   10 min.
9. Stability watch throughout: no crash, no hang, no GPU memory growth; includes one 1 h+ session.

## Provenance

- Grilling rounds 1–3 (2026-10-04): D1–D12 as listed. WAD availability and `SOS_Boom` are user-stated
  (2026-10-04); its contents are now scanner-verified (2026-10-04): 36 maps, MAP32 = 61,623 lines,
  no feature pins, UMAPINFO only sky/music/flow, custom 4-digit specials zeroed on load. `planisf2.wad`
  added the same day. Exact map + tick-list pinning remains at first bring-up.
- 2026-10-04 (user): classic-ports row (TNT/Ritual/Rampage/Spectre) and doom1.wad dropped from the
  matrix (files not on hand; doom2 + the scanned WADs above cover scale/geometry; features are
  pinned on myhouse.pk3 + Pirates! as in the lock resolution above). Soak items 2/4/8 retargeted.
  `tools/abtest/manifest.md` synced to this decision.
- Engine facts verified this session (file:line): no tick-precise capture exists — `I_GetTimeFrac()` is
  wall-clock even under `-timedemo`/`cl_capfps` (d_main.cpp:585, hw_entrypoint.cpp:319, r_utility.cpp:
  974-975); the `screenshot` command runs only from the delayed-command queue (startup, pre-level-load);
  `M_ScreenShot` (g_game.cpp) writes PNGs of the final composed framebuffer, reused for the A/B mode;
  `-playdemo`/`-record` (d_net.cpp) give path-independent input replay; `-exec`/`+cmd` (d_main.cpp:
  195-251) give per-run config via a temp autoexec; `vid_preferbackend` (v_video.cpp) selects
  GL33/Vulkan; `FrameCycles` (d_main.cpp:551, 592) already times the whole display frame; no generic
  stats cvar exists — the perf-log hook is new; the HW path has no async render thread (ticket 05
  facts), so the GPU window is the path's own fence wait; no headless mode exists (GL33 needs a display
  context, Vulkan a GPU).
- The z-fighting exception to zero tolerance is structural (ticket 04: classic draws into a normalized
  per-view depth range, levelmesh into true z) and is policed by the frozen allowlist, not relaxed.
