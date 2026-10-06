# Classic-path baseline profile on acceptance maps

Type: task
Status: claimed
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
- **Remaining (phase 3→4):** run the 52-run matrix (gl33 + vulkan,
  2560×1440, `-nomonsters`) via `tools/abtest/README.md` step 3 /
  `run_matrix.sh`, then phase 4 parse (`parse_perflog.py --warmup 350
  --window 2100`) + `L`-line cross-check against the manifest, then the
  results table posted here.
