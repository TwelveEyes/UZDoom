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

WADs (availability user-confirmed 2026-10-04) and the map sets:

- **doom1.wad** (shareware baseline): E1M2 (3D floors + doors/lifts), E1M8 (dense small), E2M2 (door
  density), E3M7 (geometry density, sky), E4M1 (scale + door density).
- **doom2.wad**: MAP01 (baseline), MAP07 (3D floors + lift), MAP21 (killfloor scale + 3D floors), MAP23
  (light thinkers), MAP27 (fog), MAP30 (3D floors + scale), MAP31 (dense scale).
- **hexen.wad**: 4–6 hand-picked maps covering at least: 2 line-portal maps, 1 sector_link map, 1 skybox
  map, 1 fog map, 1 fake-contrast map. (Category-locked; exact map numbers pinned in the committed
  manifest at first bring-up and reviewed then.)
- **heretic.wad**: 1–2 maps (skybox variant, portals).
- **Classic ports** (TNT.EXE, Ritual, Rampage, Spectre class): 1–2 maps per port chosen for 3D-floor
  density, portals, fake contrast (Spectre is the fake-contrast reference). Manifest-pinned at bring-up.
- **SOS_Boom**: the slaughter-scale map (the 200k-line class from ticket 04). 1 map; the scale payoff
  bar (decision 10) applies to it.

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
2. Full Hexen campaign (portals, sector_link, skybox, fog, fake contrast).
3. Heretic E1.
4. Ritual + Rampage tours, 10+ min each (3D-floor + portal density).
5. SOS_Boom: 15-min fast-camera fly (portal query prefetch, scale, upload budget).
6. Scripted save/load + automap fog + secret-gating walk — ticket 11's acceptance items (flag array /
   savegame diff, automap screenshot A/B, secret gating, dither + decay + cap-20, cullcolor,
   save/restore round trip, radar/ortho behavior).
7. 15-min deathmatch on a portal map (dither + OOB views + exposure under stress).
8. Light-thinker / fog / fake-contrast tour (Spectre + MAP27 + an animated-light map), 10 min.
9. Stability watch throughout: no crash, no hang, no GPU memory growth; includes one 1 h+ session.

## Provenance

- Grilling rounds 1–3 (2026-10-04): D1–D12 as listed. WAD availability and `SOS_Boom` are user-stated
  (2026-10-04); its contents (line count, feature coverage) are to be confirmed at bring-up and
  recorded in the manifest.
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
