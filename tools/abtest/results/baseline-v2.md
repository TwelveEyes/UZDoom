# Classic-path perf baseline v2

Ticket: `.scratch/levelmesh-impl/issues/02-perf-baseline-v2.md`. Raw stats: `baseline-v2.json`
(every field, p50/p95/p99/max). The v1 baseline is archived in `archive/` (see its note).

## Reference machine and protocol

- **Machine:** Ryzen 9 5900X, RX 7900 XTX (RADV NAVI31 / radeonsi), Void Linux, Wayland.
  RelWithDebInfo build, 2560×1440, vsync off. Unsandboxed, real GPU on every run.
- **Config:** `baseline.cfg` (gamma 1.0, vsync off, `vid_maxfps 0`, **`r_ticstability 0`**).
- **Tours (`demos/v2/`, 26 maps):** `make_demo.py --path tour`. 26 stops spread over the map's
  main playable area by farthest-point sampling. Each stop is a warp to (x, y, on floor) followed
  by a 100-tic, 360° view pan. Cheats: god + noclip; `-nomonsters`. Window: 350 tics warmup,
  2100 tics measured.
- **Stress vantages (`demos/vantage/`, 11 vantages on 8 maps):** `select_vantages.py`. Each one
  is the tour view with the highest sustained cost (9-tic sliding median of render CPU `r`, or of
  `wl+fl` on the small DOOM2 maps), held fixed. The record is in `vantages.json`. Window:
  105 tics warmup, 945 tics measured.
- **Monsters scenario:** SOS_Boom MAP32 + MAP12, tours and both MAP32 vantages, monsters on.
- **Non-empty-view check (`parse_perflog.py`):** median `wl+fl` ≥ 5 and ≤ 10% frames with
  `wl+fl == 0`. All 26 tours and all 11 vantages pass on both backends (worst: HEXEN MAP01, 3.2%
  empty frames). Contact sheets for every demo are in `../sheets/` (not committed; regenerate with
  `contact_sheet.py`).
- **Driver:** `run_matrix.sh` auto-terminates the engine at the timedemo end dialog
  (`wait_timedemo`).

## Findings that change how the numbers must be read

1. **`total` did not cover the frame.** `r_perflog` now has `wall` (the real interval between
   logged frames). With the engine default `r_ticstability 1`, every frame without a new sim tic
   busy-waits as long as the last tic took (`d_net.cpp` `TicStabilityWait`). On myhouse MAP01
   that is ~1.9 ms per frame; `total` covered only 43% of wall time, and the frame count dropped
   ~3×. With `r_ticstability 0`, `total` covers 90%; the rest is amortized sim tics. All v2
   numbers use `r_ticstability 0`, and the bars are stated on `wall`.
2. **The v1 cameras saw nothing.** The v1 spiral demos used `noclip2`, which turns gravity off.
   The camera kept the player-start height for the whole demo and spent most frames above
   ceilings or under floors: median 0–34 walls+flats; 0 on SOS MAP32; 6 on myhouse. The v2 tours
   reach 6–182 (myhouse 108, SOS MAP32 182).
3. **Medians hide the problem; tails and vantages show it.** Tour p50 is still ≤ 1.5 ms
   everywhere. The cost lives in p99 (SOS MAP32 28.9 ms, planisf 21.1 ms wall) and in the
   vantages (SOS MAP32 V1 47.9 ms).
4. **Where the classic render window goes** (Vulkan, p50, `r` = RenderView):

   | vantage | r | scene build `pr` | wall+flat submit `wr+fr` | sprites `ss+sr` | rest |
   |---|---|---|---|---|---|
   | SOS MAP32 V1 | 45.6 | 14.7 | 28.2 | 3.1 | -0.4 |
   | planisf V1 | 26.0 | 9.0 | 15.3 | 1.7 | -0.1 |
   | SOS MAP32 V2 | 20.8 | 7.2 | 12.1 | 1.6 | -0.2 |
   | Pirates MAP54 V1 | 6.9 | 1.1 | 3.0 | 1.4 | 1.4 (portals) |
   | myhouse MAP01 V1 | 5.9 | 0.3 | 0.6 | 0.0 | 4.9 (portal coverage) |

   - `pr` brackets the whole scene build: BSP walk + clipper (`bsp`/`clip`), worker-thread
     wall/flat setup (`ws`/`fs`), and the render hacks. `wr`/`fr` are the per-wall/per-flat
     draw submission.
   - The small negative "rest" values are timer overlap.
   - myhouse's 4.9 ms is `SetCoverage` (`hw_portal.cpp:779`). Profiled at 44% of the main
     thread: it recursively walks the **entire BSP** (132k subsectors) once per sector-stack
     portal set up in the frame, so its cost scales with level size × portals in view, and it
     sits outside the `pg` timer.
   - **The levelmesh replaces `pr`, `wr`, `fr` and the portal coverage walk**: 93–96% of the
     window on SOS MAP32 / planisf, ~85% on myhouse. Sprites stay classic (≤ 7% even with a
     2,500-sprite horde in view).
5. **Vulkan costs more CPU than GL33 for the same work.** Wall submission `wr` is 19.4 vs 11.1 ms
   (SOS V1) and the overall render window is 1.3–1.8× GL33's on the heavy vantages. The levelmesh
   is Vulkan-only, so its comparisons are Vulkan vs Vulkan.
6. **Monsters change sim cost, not render cost.** The SOS MAP32 tour's per-tic sim cost
   (wall − total, amortized) rises from ~1.2 to ~2.1 ms per tic. At the vantages, render CPU is
   unchanged within noise (V1: 45.6 → 46.6 ms Vulkan). A horde adds ~0.5 ms of sprite cost.
7. **Vulkan swapchain clamp (fixed, re-measured).** The engine's window is created at 1536×864
   and then resized to 2560×1440. The Vulkan swapchain was clamped to the surface's extent at
   creation (1536×864) and never recreated, because the client size the engine reads never
   changed. On screen, the frame covered only the top-left of the window.
   - Fixed in `vk_framebuffer.cpp`: a clamped swapchain is retried every 30 frames until it
     matches, and the event is logged.
   - The scene always rendered at 2560×1440, so the effect was small (tours within ~2 %).
   - Every Vulkan number here was re-measured after the fix; `run_matrix.sh` logs show
     "Vulkan swapchain resized to 2560x1440" on every run.
8. **Capture quirk:** `contact_sheet.py` on DOOM2 MAP23 dropped ticks 950 and 1550 (no map
   change; the perflog covers all tics). It does not affect perf data.
9. **Weak vantage:** DOOM2 MAP31 V1 faces a maze wall (42 walls+flats). It costs 0.1 ms and
   carries no weight in the bars.

## Results

### Tours, Vulkan (primary)

| run | frames | wl+fl p50 | empty | wall p50 | wall p95 | wall p99 | wall max | r p50 | r p95 | r p99 | fin p50 | fin p95 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DOOM2_MAP01 | 115986 | 18 | 0.0% | 0.487 | 0.772 | 0.866 | 15.968 | 0.081 | 0.158 | 0.214 | 0.296 | 0.477 |
| DOOM2_MAP07 | 96045 | 11 | 0.0% | 0.575 | 0.851 | 0.974 | 15.229 | 0.119 | 0.207 | 0.245 | 0.345 | 0.542 |
| DOOM2_MAP21 | 123507 | 22 | 0.0% | 0.462 | 0.714 | 0.813 | 17.004 | 0.084 | 0.149 | 0.169 | 0.269 | 0.433 |
| DOOM2_MAP23 | 115539 | 11 | 0.0% | 0.468 | 0.800 | 1.053 | 25.324 | 0.081 | 0.186 | 0.215 | 0.281 | 0.511 |
| DOOM2_MAP27 | 118534 | 8 | 0.0% | 0.472 | 0.769 | 0.871 | 16.152 | 0.070 | 0.158 | 0.205 | 0.284 | 0.468 |
| DOOM2_MAP30 | 113493 | 26 | 0.0% | 0.491 | 0.746 | 0.859 | 19.069 | 0.104 | 0.176 | 0.207 | 0.279 | 0.461 |
| DOOM2_MAP31 | 131949 | 6 | 0.0% | 0.440 | 0.538 | 0.774 | 16.260 | 0.062 | 0.099 | 0.120 | 0.270 | 0.330 |
| HERETIC_E1M2 | 118286 | 10 | 0.0% | 0.477 | 0.750 | 0.878 | 15.909 | 0.077 | 0.153 | 0.205 | 0.288 | 0.463 |
| HERETIC_E5M6 | 93223 | 26 | 0.0% | 0.560 | 1.011 | 1.177 | 15.712 | 0.131 | 0.396 | 0.501 | 0.341 | 0.554 |
| HEXEN_MAP01 | 96974 | 17 | 3.3% | 0.548 | 0.955 | 1.084 | 14.165 | 0.101 | 0.354 | 0.433 | 0.344 | 0.520 |
| HEXEN_MAP10 | 94873 | 21 | 0.0% | 0.598 | 0.916 | 1.082 | 14.332 | 0.101 | 0.263 | 0.322 | 0.391 | 0.568 |
| HEXEN_MAP27 | 99359 | 9 | 0.0% | 0.515 | 0.980 | 1.147 | 16.590 | 0.086 | 0.332 | 0.472 | 0.318 | 0.538 |
| MYHOUSE_20PAM | 100243 | 54 | 0.0% | 0.521 | 0.911 | 1.046 | 37.348 | 0.115 | 0.359 | 0.470 | 0.296 | 0.476 |
| MYHOUSE_MAP01 | 35938 | 109 | 0.0% | 1.099 | 4.945 | 7.596 | 16.465 | 0.388 | 2.809 | 5.906 | 0.436 | 0.494 |
| PIRATES_MAP43 | 63103 | 23 | 0.0% | 0.787 | 2.133 | 3.441 | 14.663 | 0.192 | 1.451 | 2.691 | 0.394 | 0.619 |
| PIRATES_MAP49 | 32741 | 78 | 0.0% | 1.413 | 5.077 | 6.570 | 14.478 | 0.709 | 4.483 | 5.924 | 0.458 | 0.584 |
| PIRATES_MAP50 | 66653 | 32 | 0.0% | 0.709 | 1.879 | 4.456 | 14.855 | 0.124 | 1.325 | 3.835 | 0.344 | 0.536 |
| PIRATES_MAP51 | 71284 | 101 | 0.0% | 0.706 | 2.273 | 2.740 | 15.738 | 0.163 | 1.740 | 2.313 | 0.282 | 0.454 |
| PIRATES_MAP54 | 45659 | 14 | 0.0% | 0.916 | 4.119 | 6.648 | 16.351 | 0.318 | 3.546 | 6.072 | 0.441 | 0.513 |
| PIRATES_MAP57 | 66642 | 56 | 0.0% | 0.748 | 1.798 | 2.307 | 14.141 | 0.128 | 1.232 | 1.634 | 0.380 | 0.519 |
| PIRATES_MAP58 | 65925 | 47 | 0.0% | 0.727 | 2.094 | 4.102 | 14.039 | 0.151 | 1.584 | 3.576 | 0.336 | 0.538 |
| PLANISF_MAP01 | 34102 | 29 | 0.0% | 0.776 | 6.851 | 21.084 | 32.064 | 0.122 | 6.152 | 19.909 | 0.357 | 0.559 |
| SOS_MAP12 | 67706 | 32 | 0.0% | 0.585 | 1.938 | 2.564 | 15.640 | 0.111 | 1.441 | 2.067 | 0.339 | 0.498 |
| SOS_MAP32 | 27748 | 182 | 0.0% | 0.879 | 5.249 | 28.901 | 62.025 | 0.259 | 4.462 | 27.057 | 0.371 | 0.482 |
| SOS_MAP45 | 66493 | 38 | 0.0% | 0.759 | 1.929 | 2.837 | 13.653 | 0.146 | 1.483 | 2.407 | 0.353 | 0.504 |
| SOS_MAP46 | 83648 | 26 | 0.0% | 0.475 | 1.735 | 4.082 | 15.426 | 0.075 | 1.231 | 3.628 | 0.273 | 0.480 |

### Tours, GL33 (reference)

| run | frames | wl+fl p50 | empty | wall p50 | wall p95 | wall p99 | wall max | r p50 | r p95 | r p99 | fin p50 | fin p95 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DOOM2_MAP01 | 145101 | 18 | 0.0% | 0.396 | 0.666 | 0.745 | 17.353 | 0.036 | 0.072 | 0.097 | 0.243 | 0.374 |
| DOOM2_MAP07 | 130372 | 12 | 0.0% | 0.436 | 0.690 | 0.779 | 36.448 | 0.053 | 0.089 | 0.109 | 0.257 | 0.435 |
| DOOM2_MAP21 | 151511 | 23 | 0.0% | 0.378 | 0.558 | 0.716 | 24.122 | 0.036 | 0.064 | 0.078 | 0.219 | 0.328 |
| DOOM2_MAP23 | 143675 | 12 | 0.0% | 0.390 | 0.659 | 0.789 | 22.431 | 0.037 | 0.084 | 0.104 | 0.226 | 0.401 |
| DOOM2_MAP27 | 149070 | 8 | 0.0% | 0.384 | 0.546 | 0.744 | 20.940 | 0.033 | 0.071 | 0.085 | 0.225 | 0.329 |
| DOOM2_MAP30 | 140416 | 27 | 0.0% | 0.400 | 0.653 | 0.778 | 33.060 | 0.047 | 0.079 | 0.094 | 0.222 | 0.408 |
| DOOM2_MAP31 | 161494 | 6 | 0.0% | 0.354 | 0.449 | 0.694 | 39.957 | 0.028 | 0.042 | 0.049 | 0.215 | 0.284 |
| HERETIC_E1M2 | 147499 | 11 | 0.0% | 0.383 | 0.649 | 0.749 | 20.766 | 0.034 | 0.068 | 0.094 | 0.224 | 0.380 |
| HERETIC_E5M6 | 117882 | 25 | 0.0% | 0.441 | 0.797 | 0.964 | 16.305 | 0.058 | 0.176 | 0.226 | 0.245 | 0.446 |
| HEXEN_MAP01 | 122982 | 18 | 3.0% | 0.423 | 0.762 | 0.881 | 17.214 | 0.045 | 0.158 | 0.187 | 0.251 | 0.429 |
| HEXEN_MAP10 | 124924 | 21 | 0.0% | 0.445 | 0.718 | 0.842 | 15.502 | 0.044 | 0.112 | 0.141 | 0.277 | 0.456 |
| HEXEN_MAP27 | 123873 | 9 | 0.0% | 0.424 | 0.780 | 0.929 | 19.378 | 0.041 | 0.142 | 0.202 | 0.248 | 0.438 |
| MYHOUSE_20PAM | 124007 | 55 | 0.0% | 0.424 | 0.748 | 0.906 | 20.658 | 0.056 | 0.173 | 0.233 | 0.228 | 0.411 |
| MYHOUSE_MAP01 | 45620 | 114 | 0.0% | 0.796 | 3.918 | 6.432 | 16.460 | 0.126 | 2.295 | 4.750 | 0.360 | 0.430 |
| PIRATES_MAP43 | 85083 | 22 | 0.0% | 0.512 | 1.567 | 2.404 | 14.729 | 0.068 | 0.852 | 1.601 | 0.264 | 0.521 |
| PIRATES_MAP49 | 46584 | 84 | 0.0% | 0.983 | 3.463 | 4.389 | 13.321 | 0.296 | 2.835 | 3.654 | 0.263 | 0.445 |
| PIRATES_MAP50 | 86277 | 33 | 0.0% | 0.478 | 1.433 | 3.114 | 15.001 | 0.058 | 0.761 | 2.478 | 0.248 | 0.438 |
| PIRATES_MAP51 | 86405 | 102 | 0.0% | 0.494 | 1.791 | 2.278 | 12.989 | 0.069 | 1.218 | 1.652 | 0.221 | 0.391 |
| PIRATES_MAP54 | 68561 | 13 | 0.0% | 0.642 | 2.690 | 4.538 | 11.437 | 0.076 | 2.062 | 3.912 | 0.279 | 0.455 |
| PIRATES_MAP57 | 84820 | 58 | 0.0% | 0.646 | 1.368 | 1.794 | 15.058 | 0.070 | 0.735 | 1.098 | 0.242 | 0.423 |
| PIRATES_MAP58 | 82959 | 48 | 0.0% | 0.518 | 1.702 | 3.088 | 15.550 | 0.067 | 1.052 | 2.417 | 0.243 | 0.422 |
| PLANISF_MAP01 | 42350 | 37 | 0.0% | 0.650 | 5.717 | 15.546 | 24.965 | 0.069 | 5.036 | 14.437 | 0.278 | 0.461 |
| SOS_MAP12 | 88878 | 42 | 0.0% | 0.463 | 1.411 | 1.878 | 16.071 | 0.057 | 0.865 | 1.320 | 0.251 | 0.413 |
| SOS_MAP32 | 36864 | 211 | 0.0% | 0.716 | 3.833 | 22.379 | 53.405 | 0.131 | 2.899 | 20.489 | 0.260 | 0.436 |
| SOS_MAP45 | 86523 | 40 | 0.0% | 0.489 | 1.499 | 2.192 | 14.729 | 0.070 | 0.942 | 1.588 | 0.263 | 0.434 |
| SOS_MAP46 | 104901 | 26 | 0.0% | 0.391 | 1.366 | 3.278 | 17.615 | 0.033 | 0.785 | 2.682 | 0.218 | 0.400 |

### Stress vantages, Vulkan (primary) — p50 per stage, ms

| run | wl+fl | sprites | wall p50 | wall p95 | r p50 | r p95 | pr (scene build) | bsp | clip | ws | wr | fr | ss | sr | pg | dcms | fin |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DOOM2_MAP27_V1 | 102 | 12 | 0.817 | 0.904 | 0.185 | 0.219 | 0.038 | 0.014 | 0.007 | 0.017 | 0.054 | 0.025 | 0.004 | 0.016 | 0.004 | 0.037 | 0.502 |
| DOOM2_MAP31_V1 | 42 | 3 | 0.452 | 0.525 | 0.087 | 0.103 | 0.020 | 0.006 | 0.002 | 0.004 | 0.032 | 0.012 | 0.001 | 0.005 | 0.000 | 0.023 | 0.262 |
| MYHOUSE_20PAM_V1 | 339 | 0 | 1.007 | 1.166 | 0.460 | 0.530 | 0.084 | 0.041 | 0.019 | 0.024 | 0.127 | 0.206 | 0.000 | 0.000 | 0.014 | 0.101 | 0.437 |
| MYHOUSE_MAP01_V1 | 613 | 15 | 7.314 | 11.424 | 5.914 | 8.939 | 0.345 | 0.295 | 0.143 | 0.125 | 0.299 | 0.314 | 0.013 | 0.035 | 0.702 | 0.213 | 0.273 |
| MYHOUSE_MAP01_V2 | 714 | 35 | 6.773 | 9.880 | 5.883 | 6.994 | 0.311 | 0.242 | 0.129 | 0.140 | 0.322 | 0.277 | 0.015 | 0.060 | 0.696 | 0.217 | 0.268 |
| PIRATES_MAP49_V1 | 2022 | 358 | 3.869 | 4.484 | 3.385 | 3.768 | 0.496 | 0.278 | 0.152 | 0.344 | 0.932 | 0.196 | 0.062 | 0.816 | 0.838 | 0.543 | 0.305 |
| PIRATES_MAP54_V1 | 4370 | 1054 | 7.678 | 9.245 | 6.944 | 8.341 | 1.127 | 0.584 | 0.344 | 0.735 | 2.563 | 0.439 | 0.192 | 1.245 | 1.261 | 1.014 | 0.457 |
| PLANISF_MAP01_V1 | 27786 | 1263 | 27.277 | 30.360 | 25.952 | 29.111 | 9.046 | 7.628 | 5.110 | 6.567 | 15.056 | 0.267 | 0.455 | 1.268 | 0.024 | 2.524 | 0.397 |
| PLANISF_MAP01_V2 | 25206 | 1010 | 24.215 | 26.771 | 22.989 | 25.382 | 7.311 | 5.975 | 4.039 | 5.308 | 12.449 | 1.945 | 0.374 | 1.033 | 0.020 | 2.571 | 0.360 |
| SOS_MAP32_V1 | 46176 | 2108 | 47.886 | 50.995 | 45.575 | 48.611 | 14.669 | 12.029 | 7.913 | 7.787 | 18.075 | 10.170 | 0.860 | 2.246 | 0.028 | 4.616 | 0.277 |
| SOS_MAP32_V2 | 19299 | 1195 | 22.485 | 24.729 | 20.820 | 22.806 | 7.228 | 4.623 | 2.941 | 3.548 | 8.588 | 3.529 | 0.413 | 1.226 | 0.033 | 1.974 | 0.242 |

### Stress vantages, GL33 (reference) — p50 per stage, ms

| run | wl+fl | sprites | wall p50 | wall p95 | r p50 | r p95 | pr (scene build) | bsp | clip | ws | wr | fr | ss | sr | pg | dcms | fin |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DOOM2_MAP27_V1 | 102 | 12 | 0.511 | 0.715 | 0.080 | 0.106 | 0.037 | 0.014 | 0.007 | 0.016 | 0.017 | 0.006 | 0.003 | 0.004 | 0.001 | 0.002 | 0.274 |
| DOOM2_MAP31_V1 | 42 | 3 | 0.380 | 0.518 | 0.037 | 0.049 | 0.018 | 0.006 | 0.002 | 0.004 | 0.007 | 0.002 | 0.001 | 0.001 | 0.000 | 0.001 | 0.211 |
| MYHOUSE_20PAM_V1 | 339 | 0 | 0.776 | 0.932 | 0.211 | 0.277 | 0.083 | 0.041 | 0.019 | 0.023 | 0.038 | 0.072 | 0.000 | 0.000 | 0.002 | 0.003 | 0.359 |
| MYHOUSE_MAP01_V1 | 613 | 15 | 5.785 | 9.824 | 4.660 | 7.756 | 0.324 | 0.280 | 0.137 | 0.113 | 0.116 | 0.060 | 0.011 | 0.013 | 0.058 | 0.016 | 0.253 |
| MYHOUSE_MAP01_V2 | 714 | 35 | 5.179 | 8.535 | 4.350 | 5.829 | 0.310 | 0.240 | 0.132 | 0.143 | 0.098 | 0.053 | 0.015 | 0.016 | 0.082 | 0.015 | 0.231 |
| PIRATES_MAP49_V1 | 2022 | 358 | 2.478 | 3.178 | 1.889 | 2.439 | 0.433 | 0.266 | 0.144 | 0.286 | 0.397 | 0.169 | 0.048 | 0.460 | 0.356 | 0.172 | 0.236 |
| PIRATES_MAP54_V1 | 4370 | 1054 | 5.315 | 6.844 | 4.601 | 5.942 | 1.149 | 0.658 | 0.386 | 0.729 | 1.591 | 0.380 | 0.192 | 0.532 | 0.663 | 0.370 | 0.256 |
| PLANISF_MAP01_V1 | 27786 | 1263 | 20.317 | 23.310 | 18.978 | 22.040 | 8.135 | 7.687 | 5.162 | 5.463 | 9.668 | 0.223 | 0.464 | 0.691 | 0.018 | 0.562 | 0.327 |
| PLANISF_MAP01_V2 | 25206 | 1010 | 17.415 | 19.942 | 16.251 | 18.759 | 6.358 | 5.865 | 3.936 | 4.484 | 7.902 | 1.262 | 0.351 | 0.560 | 0.014 | 0.556 | 0.201 |
| SOS_MAP32_V1 | 46176 | 2108 | 39.462 | 44.140 | 37.098 | 41.432 | 15.231 | 12.629 | 8.281 | 7.601 | 11.082 | 8.795 | 0.853 | 1.387 | 0.023 | 1.293 | 0.334 |
| SOS_MAP32_V2 | 19299 | 1195 | 18.161 | 21.857 | 16.467 | 19.752 | 7.267 | 4.988 | 3.173 | 3.325 | 5.540 | 2.519 | 0.412 | 0.695 | 0.026 | 0.502 | 0.328 |

### Monsters scenario (with monsters; compare the matching rows above)

| run | sprites | wall p50 | wall p95 | wall p99 | r p50 | r p95 | ss | sr |
|---|---|---|---|---|---|---|---|---|
| mon_SOS_MAP12_gl33 | 8 | 0.478 | 1.527 | 2.056 | 0.062 | 0.912 | 0.002 | 0.003 |
| mon_SOS_MAP12_vulkan | 8 | 0.662 | 2.032 | 2.788 | 0.122 | 1.503 | 0.002 | 0.011 |
| mon_SOS_MAP32_gl33 | 10 | 0.740 | 4.417 | 23.751 | 0.141 | 2.994 | 0.003 | 0.003 |
| mon_SOS_MAP32_vulkan | 10 | 0.882 | 5.740 | 29.957 | 0.265 | 4.360 | 0.003 | 0.011 |
| monvant_SOS_MAP32_V1_gl33 | 2514 | 40.859 | 45.656 | 47.370 | 37.023 | 41.249 | 1.036 | 1.767 |
| monvant_SOS_MAP32_V1_vulkan | 2514 | 50.459 | 54.073 | 55.766 | 46.596 | 49.494 | 1.047 | 2.689 |
| monvant_SOS_MAP32_V2_gl33 | 1365 | 18.945 | 21.427 | 22.948 | 16.289 | 18.558 | 0.458 | 0.839 |
| monvant_SOS_MAP32_V2_vulkan | 1365 | 24.434 | 27.064 | 28.869 | 21.669 | 24.067 | 0.505 | 1.428 |
