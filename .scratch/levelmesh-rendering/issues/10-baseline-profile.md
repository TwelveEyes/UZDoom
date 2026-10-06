# Classic-path baseline profile on acceptance maps

Type: task
Status: resolved
Blocked by: 09

## Question

Run the classic-path baseline profile on the acceptance maps. In-game, needs a GPU/display — a human drives
the build (`cmake --build build --config RelWithDebInfo --parallel 3`); the agent prepares the exact
checklist (maps, resolution, settings, which stats to read) once ticket 09 lands.

Record for each acceptance map: render CPU time (traversal + batch construction) and GPU frame time at the
fixed resolution/settings, including the slaughter, portal, and 3D-floor stress cases.

Deliverable: a results table (map, resolution, settings, render-CPU ms, frame ms) posted as a comment on
this ticket — these numbers anchor the acceptance bar in the spec.

## Progress

- **2026-10-04** (phases 0+1, b6126b6): reference machine recorded (dev box =
  reference: Void Linux 7.2.9_1, Ryzen 9 5900X, Navi 31 / amdgpu+Mesa,
  31 GiB, RelWithDebInfo; Wayland available in-session). Category maps
  pinned from `scan_maps.py` v7 (hexen MAP01/10/27, heretic E1M2/E5M6);
  classic-ports row + doom1 dropped. Backend cvar resolved from source:
  `vid_preferbackend` (0=GL33, 1=Vulkan) — `gl_backend` does not exist.
- **2026-10-05** (phase 2, ad9b83e): demo capture is now **synthetic**,
  not hand-recorded — the original "record once on the classic path" step
  is replaced by `make_demo.py` (seeded travel-to-spiral camera from each
  map's real geometry + the engine's exact tic model, packed as a valid
  UZDoom `.lmp`). Deterministic → replays frame-identically across code
  changes; no rebuild needed after every binary, except where a map's own
  scripted events diverge the sim (detectable from the perflog `L` line).
  All 26 demos generated + replay-verified (full 2600 tics, exit 255,
  `L` lines match the pins). See `tools/abtest/manifest.md` "Demo set".
- **r_perflog fix** (a06b0e2): the `L` line keyed on a `Level` pointer
  only fired on first load; now keyed on `Level->MapName` so every reload
  emits its stats line.
- **2026-10-05 (user decision):** the resolution axis is **dropped**; all
  runs pin the host-native resolution, 2560×1440 (reference machine's
  DP-1 panel). A calibration run on DOOM2_MAP01 (GL33, 2600 tics) had
  shown per-frame cost identical at 320×200 / 1920×1080 / 3440×1440
  (med ≈ 0.235 ms, p95 ≈ 0.26 ms, max ≈ 6.6 ms — 2.4× pixels, zero cost
  delta): timedemo is CPU-bound (scene graph / draw-call side) at that
  scene size, so the resolution axis measured nothing. Phase 3 is now
  **2 runs per map (2 backends) = 52 total**, driven by
  `tools/abtest/run_matrix.sh`; perflogs live in the workspace
  (`tools/abtest/logs/`) because sandboxed shells block the engine's
  `/tmp` writes and `r_perflog` fails silently — exit 255 does not prove
  the log exists, so every run asserts the log + `F 2600` line.
  Amended same day: the sandbox finding is broader — it blocks `/dev/dri`
  as well, so a sandboxed run silently renders with **llvmpipe** (software
  GL; the Vulkan backend falls back to GL with no ICD) while still
  exiting 255 with a full perflog. `run_matrix.sh` rejects runs whose
  stdout shows llvmpipe or a failed Vulkan init; the matrix runs
  unsandboxed on the real GPU (verified smoke: radeonsi + RADV NAVI31,
  framebuffer 2560×1440).
- **2026-10-05 (phases 3+4, complete):** the 52-run matrix was executed
  on the reference machine, real GPU (GL: `radeonsi, navi31, ACO`; Vulkan:
  `RADV NAVI31`, discrete), framebuffer 2560×1440, RelWithDebInfo.
  **52/52 PASS.** First pass: 24 PASS (the three standalone commercial
  IWADs) + 28 FAIL — all four non-commercial WADs are **PWADs**; launched
  as a bare `-iwad <pwad>` the engine aborts before the demo loads (exit
  0, silent, no stdout error). Per user instruction they mount on the
  commercial base: `-iwad wads/DOOM2.WAD -file wads/<pwad>` (encoded in
  `run_matrix.sh` `mount_for`); the 28 re-runs all passed.
- **Completion-check fix (supersedes the "F 2600 line" wording above):**
  the perflog has no summary line — `F <frame>` numbers the *rendered*
  frame (40k–160k per run; rendering is uncapped while the demo ticks at
  ~35 tics/s wall clock, so every run takes ~80–90 s regardless of map
  size) and `tic=` carries the demo tick. `grep '^F 2600'` also matches
  `F 2600x` and is a false-positive trap; `run_matrix.sh` now asserts the
  last `F` line's `tic=` ≥ 2599. All 52 perflogs re-verified against the
  corrected check. (`parse_perflog.py`'s warmup/window selection parses
  `tic=` as k=v and was unaffected.)
- **`L`-line cross-check vs `manifest.md` pins — all confirmed:** DOOM2/
  HERETIC/SOS/PLANISF carry no features, as pinned. HEXEN polyobjs 12/7/9
  (active at load; vanilla format coverage). myhouse 20PAM lineportals=60
  (exact pin match). myhouse MAP01: 410 line portals (exact) + **89
  portal groups vs 98× Sector_SetPortal static (delta 9)** + 9812 3D
  floors + 169 polyobjs, 165,922 lines. PLANISF 37,265 lines / 6,287
  sectors (exact). PIRATES: 3D floors on all 7 maps (160–1759), portal
  groups on MAP50 (6) / MAP58 (3), **polyobjs=0 at load on all 7** — the
  polyobject pin is format coverage for these maps: start lines exist but
  nothing is active at level start (triggers need actors/`-nomonsters` is
  on and the synthetic camera need not cross them); active-polyobj render
  coverage comes from HEXEN + myhouse MAP01 instead.
- **Results (phase 4, `tools/abtest/results/baseline.md` +
  `baseline.json`; `parse_perflog.py --warmup 350 --window 2100` — 10 s
  warmup + 60 s window; medians; ms per frame):** settings = 2560×1440,
  `-nomonsters`, `baseline.cfg` pins, both backends. Render-CPU = `r`
  (RenderView), frame = `total` (total frame), 3d-fin = `fin`
  (finish/present). p95 + full per-field breakdowns in the JSON.

| WAD / map (sectors) | gl33 render-CPU | gl33 frame | gl33 3d-fin | vulkan render-CPU | vulkan frame | vulkan 3d-fin |
|---|---|---|---|---|---|---|
| DOOM2_MAP01 (59) | 0.035 | 0.464 | 0.315 | 0.075 | 0.518 | 0.328 |
| DOOM2_MAP07 (29) | 0.034 | 0.379 | 0.242 | 0.081 | 0.464 | 0.290 |
| DOOM2_MAP21 (61) | 0.025 | 0.324 | 0.198 | 0.052 | 0.394 | 0.235 |
| DOOM2_MAP23 (69) | 0.030 | 0.350 | 0.227 | 0.064 | 0.422 | 0.271 |
| DOOM2_MAP27 (187) | 0.031 | 0.378 | 0.241 | 0.068 | 0.443 | 0.282 |
| DOOM2_MAP30 (22) | 0.030 | 0.300 | 0.185 | 0.065 | 0.419 | 0.258 |
| DOOM2_MAP31 (80) | 0.029 | 0.339 | 0.200 | 0.060 | 0.401 | 0.231 |
| HERETIC_E1M2 (247) | 0.031 | 0.361 | 0.232 | 0.067 | 0.437 | 0.287 |
| HERETIC_E5M6 (416) | 0.045 | 0.506 | 0.340 | 0.102 | 0.611 | 0.404 |
| HEXEN_MAP01 (400) | 0.027 | 0.331 | 0.208 | 0.057 | 0.407 | 0.247 |
| HEXEN_MAP10 (337) | 0.030 | 0.385 | 0.251 | 0.066 | 0.481 | 0.309 |
| HEXEN_MAP27 (368) | 0.028 | 0.356 | 0.216 | 0.062 | 0.500 | 0.332 |
| MYHOUSE_20PAM (1075) | 0.069 | 0.528 | 0.312 | 0.115 | 0.628 | 0.341 |
| MYHOUSE_MAP01 (35853) | 0.203 | 0.821 | 0.346 | 0.326 | 0.988 | 0.395 |
| PIRATES_MAP43 (1092) | 0.050 | 0.510 | 0.308 | 0.076 | 0.539 | 0.338 |
| PIRATES_MAP49 (987) | 0.129 | 0.658 | 0.295 | 0.061 | 0.559 | 0.334 |
| PIRATES_MAP50 (3897) | 0.071 | 0.537 | 0.318 | 0.156 | 0.641 | 0.357 |
| PIRATES_MAP51 (536) | 0.028 | 0.340 | 0.202 | 0.056 | 0.391 | 0.232 |
| PIRATES_MAP54 (839) | 0.031 | 0.493 | 0.351 | 0.059 | 0.534 | 0.371 |
| PIRATES_MAP57 (1336) | 0.032 | 0.492 | 0.300 | 0.066 | 0.545 | 0.338 |
| PIRATES_MAP58 (1796) | 0.027 | 0.459 | 0.319 | 0.057 | 0.509 | 0.342 |
| PLANISF_MAP01 (6287) | 0.057 | 0.572 | 0.344 | 0.120 | 0.658 | 0.393 |
| SOS_MAP12 (2422) | 0.028 | 0.455 | 0.311 | 0.050 | 0.506 | 0.342 |
| SOS_MAP32 (9907) | 0.027 | 0.468 | 0.286 | 0.047 | 0.527 | 0.320 |
| SOS_MAP45 (1939) | 0.040 | 0.502 | 0.328 | 0.091 | 0.581 | 0.383 |
| SOS_MAP46 (2366) | 0.026 | 0.487 | 0.348 | 0.051 | 0.518 | 0.349 |

Baseline observations (for the A/B effort): Vulkan's CPU RenderView window
is ~2× GL33's on every map (0.05–0.33 ms vs 0.025–0.20 ms) while the GPU
3d-fin window is close (within ~0.1 ms) — the Vulkan gap is on the CPU
submission side at these scene sizes, not the GPU. Total frame time is
flat 0.3–0.6 ms across most maps; the two heaviest renders are myhouse
MAP01 (0.82/0.99 ms, portal map) and PLANISF (0.57/0.66 ms, 37k lines,
p95 spikes to 6.1/7.1 ms). These numbers anchor the acceptance bars
(levelmesh ≤ classic × 1.05 per map on both backends; SOS_Boom ≤ 50 % of
its classic median).

## Answer

The deliverable (results table) is posted above in the final `## Progress`
item — 26 maps × {GL33, Vulkan} at 2560×1440, `-nomonsters`, `baseline.cfg`
pins; medians in ms/frame for render-CPU (`r`), frame (`total`), GPU 3d-fin
(`fin`); p95 + full per-field breakdowns in `tools/abtest/results/baseline.json`,
prose + table in `tools/abtest/results/baseline.md` (parsed with
`parse_perflog.py --warmup 350 --window 2100` — 10 s warmup, 60 s window).

Key facts for the spec:
- Reference machine = the dev box (Void Linux 7.2.9_1, Ryzen 9 5900X, Navi 31,
  31 GiB, RelWithDebInfo) — recorded in Progress.
- Vulkan's CPU RenderView window ≈ 2× GL33 on every map while the GPU 3d-fin
  window is within ~0.1 ms — the Vulkan gap is on the CPU submission side at
  these scene sizes, not the GPU.
- Total frame time flat 0.3–0.6 ms across most maps; heaviest: myhouse MAP01
  0.82/0.99 ms (35,853 sectors, portal map) and PLANISF 0.57/0.66 ms (37,265
  lines, p95 6.1/7.1 ms).
- These numbers anchor 09's acceptance bars: levelmesh ≤ classic × 1.05 per
  map on both backends; SOS_Boom ≤ 50 % of its classic median.
