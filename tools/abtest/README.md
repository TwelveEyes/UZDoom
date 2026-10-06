# levelmesh A/B & baseline tooling

Tooling for tickets 09/10 of `.scratch/levelmesh-rendering/`: the classic
performance baseline (ticket 10) and, later, the A/B capture + comparison
driver (implemented with ticket 09's tooling). This directory is the
committed home for demos, manifests, allowlists, and scripts; large perf
logs stay local (`logs/`, `runs/` are gitignored).

## What exists now (ticket 10: classic baseline)

- **`r_perflog <path>` cvar** — engine-side (see `hw_clock.cpp`): opens a
  file and enables the existing per-frame HW render profiler
  (no on-screen overlay). One line per frame (tic, total frame, RenderView
  window, BSP/clip/wall/flat/sprite/2D/flush3d/finish breakdown, draw
  counts) plus one summary line per loaded level (map, sectors, lines,
  subsectors, sprites, polyobjs, line portals, portal groups, 3D floors).
  Flushes per frame, so the data survives the `I_FatalError` exit that
  ends a timedemo. Disabled by setting the cvar back to `""`.
- **`baseline.cfg`** — the fixed cvar pins for all baseline/perf runs.
- **`scan_maps.py`** — static WAD feature scanner (stdlib only) for the
  verify-first map pinning.
- **`make_demo.py`** — synthetic demo generator (stdlib only): computes a
  seeded travel-to-spiral camera path from the map's real geometry + the
  engine's exact tic movement model, and packs a valid UZDoom `.lmp`
  (demo_version 114, `demo_compression`, `-nomonsters`-compatible).
- **`parse_perflog.py`** — perflog → median/p95 table (stdlib only).
- **`manifest.md`** — the map/resolution/machine manifest (all pins
  locked + reference machine recorded 2026-10-04, bring-up phase 0/1 done;
  demo set + scanner v8 corrections recorded 2026-10-05).
- **`demos/*.lmp`** — the 26 generated baseline demos (see the manifest's
  "Demo set" section).

## Protocol (from ticket 09)

- Demo replay: one ~70-80 s demo per acceptance map, **generated once from
  the map's geometry** (`make_demo.py`; deterministic, seeded, replays
  identically across code changes). 10 s warmup + 60 s measurement window
  (350 + 2100 tics).
- Per map × backend: `timedemo` replay at uncapped speed with
  `r_perflog` writing to `logs/<map>_<backend>.perflog`.
- Backends: **both** GL 3.3 and Vulkan must be measured.
- Resolution: **host-native 2560×1440** (vsync off). The resolution axis
  was dropped 2026-10-05 (user decision): a calibration run on
  DOOM2_MAP01 (GL33, 2600 tics) showed per-frame cost identical at
  320×200, 1920×1080, and 3440×1440 (med ≈ 0.235 ms, p95 ≈ 0.26 ms,
  max ≈ 6.6 ms — a 2.4× pixel difference with zero cost difference).
  Timedemo is CPU-bound (scene graph / draw-call side) at that scene
  size, so the axis measured nothing; every run pins the host-native
  panel instead.
- Gamma 1.0 / default cvars (pinned in `baseline.cfg`).
- Stats read: median + p95 of total frame, CPU render window
  (`r` = RenderView), GPU 3D window (`fin` = finish/present) — the full
  per-field table is in the parsed output.
- Bars (checked later by the A/B effort, not here): levelmesh ≤ classic ×
  1.05 per map on both backends; SOS_Boom ≤ 50 % of its classic median.

## Steps

### 0 — Bring-up (human)

1. Build:

   ```bash
   cmake --build build --config RelWithDebInfo --parallel 3
   ```
2. Drop the WAD set into `build/wads/` (see `manifest.md` for the list).
3. Record the reference-machine facts in `manifest.md` (CPU/GPU SKUs,
   driver, kernel, build type).

### 1 — Pin the category maps (agent)

```bash
python3 tools/abtest/scan_maps.py build/wads/*.wad   # run from the build dir
```

DONE (2026-10-04, phase 1): proposed from the `scan_maps.py` v7 output,
user-approved, written into `manifest.md` — hexen MAP01/MAP10/MAP27, heretic
E1M2/E5M6. The classic-ports row (TNT/Ritual/Rampage/Spectre) was dropped
from the matrix the same day (see manifest.md).

### 2 — Generate demos (agent/human, once per map; already done 2026-10-05)

The demos in `demos/` are synthesized (no human recording): each is a
seeded camera flight over the map computed from its real geometry — the
same movement math the engine uses, so replay is exact. Generate one:

```bash
python3 tools/abtest/make_demo.py --iwad build/wads/HEXEN.WAD --map MAP10 \
    --out tools/abtest/demos/HEXEN_MAP10.lmp --tics 2600
```

`--map` is the engine's map name (Heretic: `E1M2`); for PK3s pass the PK3
itself as `--iwad`. A warning in the output means the map's geometry or
player start was not found — regenerate after fixing the WAD, don't ship a
fall-back demo. `--dry-run` prints the plan without writing. Naming:
`<WAD>_<MAP>.lmp` (see the manifest's "Demo set" section).

Smoke-test a demo before relying on it (exit 255 = normal timedemo end):

```bash
cd build && printf 'r_perflog ../tools/abtest/logs/smoke.log\n' > ../tools/abtest/logs/smoke.cfg
./uzdoom -iwad <iwadfile> [-file <pwads...>] -nomonsters -devparm \
    -width 640 -height 400 -exec ../tools/abtest/logs/smoke.cfg -timedemo ../tools/abtest/demos/<demo>.lmp
grep '^L ' ../tools/abtest/logs/smoke.log
awk '$1=="F"{t=$3} END{sub("tic=","",t); print "last tic: " t+0}' \
    ../tools/abtest/logs/smoke.log   # demo complete if >= 2599
```

- The perflog must live **inside the workspace** (`tools/abtest/logs/`):
  sandboxed shells block the engine's writes to `/tmp`, and `r_perflog`
  then fails **silently** — exit 255 does not prove the log was written.
  After every run, assert the log exists and the demo played through
  (last `F` line's `tic=` >= 2599 for the 2600-tic demos) before
  accepting the run. There is no "F 2600 summary line": `F <frame>`
  numbers the rendered frame (tens of thousands per run — rendering is
  uncapped) and `tic=` carries the demo tick; `grep '^F 2600'` also
  matches `F 2600x` and is a false-positive trap.

### 3 — Perf runs (agent; 2 backends × 1 resolution = 2 per map, 52 total)

Prefer the driver: `bash tools/abtest/run_matrix.sh` (full matrix,
sequential; `--only NAME` filters demo names, repeatable, OR-match;
`--backend gl33|vulkan`). It writes `logs/status.tsv` (demo, backend,
exit, verdict, wall_s, lline) and rejects llvmpipe/Vulkan-fallback runs.
Single run, by hand:

```bash
./uzdoom -iwad <iwadfile> [-file <pwads...>] -nomonsters \
    -width 2560 -height 1440 \
    -exec tools/abtest/baseline.cfg \
    +set vid_preferbackend <N> \
    +set r_perflog tools/abtest/logs/<map>_<backend>.perflog \
    -timedemo tools/abtest/demos/<map>.lmp
```

(Use absolute paths for `-exec`, `r_perflog`, and `-timedemo` when the
engine's cwd differs from the repo root, as in `run_matrix.sh`.)

- **PWAD mount:** the four non-commercial WADs (myhouse, Pirates!,
  planisf2, SOS_Boom) are PWADs and must be mounted on the commercial
  base: `-iwad wads/DOOM2.WAD -file wads/<pwad>`. Launched as a bare
  `-iwad <pwad>` the engine aborts before the demo loads (exit 0, silent,
  no stdout error; the Vulkan variant exits 255 with an empty perflog).
  The three commercial IWADs (DOOM2/HEXEN/HERETIC) run standalone.
  `run_matrix.sh` encodes this mapping in `mount_for`.
- **~80–90 s per run, regardless of map size:** the timedemo plays the
  demo back at the game's tic rate (~35 tics/s wall clock) while rendering
  runs uncapped (40k–160k frames per 2600-tic demo). The full 52-run
  matrix takes ~75 minutes.

- Resolution is set with the **`-width`/`-height` command-line FARGs**
  (v_video.cpp:130-141). `vid_width`/`vid_height` are **not** cvars in this
  engine (only the `vid_defwidth`/`vid_defheight` fallback defaults exist,
  v_video.cpp:174) — an earlier draft of this README used a non-existent
  `+set vid_width`, which silently did nothing.
- `<backend>`: `0` = GL 3.3 core, `1` = Vulkan. The cvar is
  `vid_preferbackend` (v_video.cpp:85; BACKEND_OPENGL=0 / BACKEND_VULKAN=1,
  v_video.h:70). There is no `gl_backend` cvar in the engine. The engine
  auto-selects a backend at startup (Vulkan when available), so set the
  cvar explicitly in every run; stdout prints "Selecting ... backend...",
  which each run's captured output is checked against.
- Resolution is pinned at **2560×1440** — the host-native panel of the
  reference machine (DP-1; a 1920×1080 panel is also connected, virtual
  desktop 4480×1440). The 3-resolution axis (1080p / 320×200 / 21:9) was
  dropped 2026-10-05, see the protocol section above.
- `-nomonsters` on every run: the demo set is built for it (the camera path
  is not designed around monster combat/deaths) and it keeps the render
  workload deterministic.
- timedemo exits with code **255** (an `I_FatalError`-style shutdown) on
  success — do not treat 255 as a failure; the perflog is complete because
  `r_perflog` flushes per frame.
- Runs must have real GPU access. A sandboxed shell blocks `/dev/dri`, and
  the engine then **silently** renders with llvmpipe (software GL) — the
  Vulkan backend included, which finds no ICD and falls back to GL. Such
  runs still exit 255 with a full perflog, but the numbers are software
  numbers, not GPU numbers. `run_matrix.sh` rejects any run whose stdout
  shows `llvmpipe` or a failed Vulkan init; run the matrix unsandboxed
  (or from a normal shell).

### 4 — Parse + verify (agent)

```bash
python3 tools/abtest/parse_perflog.py --warmup 350 --window 2100 \
    --md tools/abtest/results/baseline.md tools/abtest/logs/*.perflog
```

Cross-check each map's `L` line (portal groups, line portals, 3D floors,
polyobjs) against the static scan and the category expectations from
`manifest.md`; flag any mismatch. The results table is posted as a comment
on ticket 10.

## Log format (`r_perflog v1`)

See the `#` header lines written at the top of every perflog file; the
field meanings are documented there. `L` lines precede the `F` frames of
their level; frames between two `L` lines belong to the most recent `L`.
