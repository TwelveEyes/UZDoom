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
- Per map × backend × resolution: `timedemo` replay at uncapped speed with
  `r_perflog` writing to `logs/<map>_<backend>_<res>.perflog`.
- Backends: **both** GL 3.3 and Vulkan must be measured.
- Resolutions: 1920×1080 (vsync off), 320×200 clean, 3440×1440 (21:9
  stress widescreen).
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
cd build && printf 'r_perflog /tmp/smoke.log\n' > /tmp/smoke.cfg
./uzdoom -iwad <iwadfile> [-file <pwads...>] -nomonsters -devparm \
    -width 640 -height 400 -exec /tmp/smoke.cfg -timedemo ../tools/abtest/demos/<demo>.lmp
grep -E '^L |F 2600|255' /tmp/smoke.log
```

### 3 — Perf runs (human; 2 backends × 3 resolutions = 6 per map)

```bash
./uzdoom -iwad <iwadfile> -file <pwads...> -exec tools/abtest/baseline.cfg \
    +set vid_width <W> +set vid_height <H> +set vid_preferbackend <N> \
    +set r_perflog tools/abtest/logs/<map>_<backend>_<res>.perflog \
    +timedemo tools/abtest/demos/<map>.lmp
```

- `<backend>`: `0` = GL 3.3 core, `1` = Vulkan. The cvar is
  `vid_preferbackend` (v_video.cpp:85; BACKEND_OPENGL=0 / BACKEND_VULKAN=1,
  v_video.h:70). There is no `gl_backend` cvar in the engine.
- `<res>`: `1080p` (1920×1080), `320x200`, `21x9` (3440×1440).
- timedemo exits with code **255** (an `I_FatalError`-style shutdown) on
  success — do not treat 255 as a failure; the perflog is complete because
  `r_perflog` flushes per frame.

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
