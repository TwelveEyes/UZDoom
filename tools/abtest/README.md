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
- **`parse_perflog.py`** — perflog → median/p95 table (stdlib only).
- **`manifest.md`** — the map/resolution/machine manifest (pins fill in
  during bring-up).

## Protocol (from ticket 09)

- Demo replay: one ~70-80 s demo per acceptance map, **recorded once on
  the classic path**. 10 s warmup + 60 s measurement window
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

I propose the Hexen/Heretic/TNT/Ritual/Rampage/Spectre pins from the
scan; the user approves; the pins are written into `manifest.md`.

### 2 — Record demos (human, once per map, classic path)

From the build directory:

```bash
./uzdoom -iwad <iwadfile> -file <pwads...> -exec tools/abtest/baseline.cfg \
    -savedir tools/abtest/runs/<map> -warp <map> -record <map>
```

Play ~80 s of normal traversal (the parser slices warmup+window by tic, so
a slight overshoot is fine), then type `stop` in the console. Copy the
demo into the repo:

```bash
cp tools/abtest/runs/<map>/<map>.lmp tools/abtest/demos/<map>.lmp
```

### 3 — Perf runs (human; 3 backends×resolutions = 6 per map)

```bash
./uzdoom -iwad <iwadfile> -file <pwads...> -exec tools/abtest/baseline.cfg \
    +set vid_width <W> +set vid_height <H> +set gl_backend <N> \
    +set r_perflog tools/abtest/logs/<map>_<backend>_<res>.perflog \
    +timedemo tools/abtest/demos/<map>.lmp
```

- `<backend>`: `gl33` / `vk` — `gl_backend` values to be confirmed at
  bring-up (GL 3.3 core vs Vulkan).
- `<res>`: `1080p` (1920×1080), `320x200`, `21x9` (3440×1440).

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
