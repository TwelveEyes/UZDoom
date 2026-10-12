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
   everywhere. The cost lives in p99 (SOS MAP32 28.5 ms, planisf 21.2 ms wall) and in the
   vantages (SOS MAP32 V1 49.8 ms).
4. **Where the classic render window goes** (Vulkan, p50, `r` = RenderView):

   | vantage | r | scene build `pr` | wall+flat submit `wr+fr` | sprites `ss+sr` | rest |
   |---|---|---|---|---|---|
   | SOS MAP32 V1 | 47.6 | 15.1 | 29.9 | 3.3 | -0.7 |
   | planisf V1 | 26.3 | 9.2 | 15.5 | 1.8 | -0.3 |
   | SOS MAP32 V2 | 21.2 | 7.4 | 12.3 | 1.7 | -0.2 |
   | Pirates MAP54 V1 | 6.8 | 1.1 | 3.0 | 1.4 | 1.3 (portals) |
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
   unchanged within noise (V1: 47.6 → 46.7 ms Vulkan). A horde adds ~0.5 ms of sprite cost.
7. **Capture quirk:** `contact_sheet.py` on DOOM2 MAP23 dropped ticks 950 and 1550 (no map
   change; the perflog covers all tics). It does not affect perf data.
8. **Weak vantage:** DOOM2 MAP31 V1 faces a maze wall (42 walls+flats). It costs 0.1 ms and
   carries no weight in the bars.

## Results

### Tours, Vulkan (primary)

| run | frames | wl+fl p50 | empty | wall p50 | wall p95 | wall p99 | wall max | r p50 | r p95 | r p99 | fin p50 | fin p95 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DOOM2_MAP01 | 112938 | 17 | 0.0% | 0.499 | 0.773 | 0.873 | 12419.558 | 0.083 | 0.163 | 0.219 | 0.302 | 0.484 |
| DOOM2_MAP07 | 94044 | 11 | 0.0% | 0.587 | 0.851 | 0.980 | 14.857 | 0.124 | 0.209 | 0.246 | 0.351 | 0.541 |
| DOOM2_MAP21 | 122005 | 22 | 0.0% | 0.463 | 0.719 | 0.810 | 16.671 | 0.084 | 0.153 | 0.179 | 0.269 | 0.440 |
| DOOM2_MAP23 | 110525 | 11 | 0.0% | 0.485 | 0.844 | 1.074 | 16.354 | 0.085 | 0.190 | 0.222 | 0.284 | 0.523 |
| DOOM2_MAP27 | 117362 | 8 | 0.0% | 0.475 | 0.773 | 0.889 | 15.677 | 0.071 | 0.161 | 0.208 | 0.283 | 0.472 |
| DOOM2_MAP30 | 114345 | 26 | 0.0% | 0.487 | 0.745 | 0.867 | 52.849 | 0.106 | 0.183 | 0.205 | 0.277 | 0.461 |
| DOOM2_MAP31 | 130886 | 6 | 0.0% | 0.443 | 0.544 | 0.781 | 16.894 | 0.063 | 0.101 | 0.128 | 0.269 | 0.329 |
| HERETIC_E1M2 | 117242 | 10 | 0.0% | 0.477 | 0.760 | 0.875 | 20.942 | 0.077 | 0.159 | 0.210 | 0.288 | 0.469 |
| HERETIC_E5M6 | 91069 | 26 | 0.0% | 0.582 | 1.038 | 1.189 | 14.233 | 0.136 | 0.416 | 0.539 | 0.347 | 0.552 |
| HEXEN_MAP01 | 96681 | 17 | 3.2% | 0.548 | 0.966 | 1.102 | 19.882 | 0.102 | 0.358 | 0.441 | 0.343 | 0.515 |
| HEXEN_MAP10 | 93223 | 21 | 0.0% | 0.610 | 0.914 | 1.058 | 6.472 | 0.104 | 0.265 | 0.318 | 0.393 | 0.569 |
| HEXEN_MAP27 | 94945 | 9 | 0.0% | 0.535 | 1.000 | 1.159 | 16.802 | 0.089 | 0.336 | 0.466 | 0.320 | 0.540 |
| MYHOUSE_20PAM | 99516 | 54 | 0.0% | 0.522 | 0.922 | 1.065 | 18.347 | 0.115 | 0.362 | 0.466 | 0.297 | 0.476 |
| MYHOUSE_MAP01 | 34945 | 108 | 0.0% | 1.113 | 5.128 | 8.323 | 14.771 | 0.388 | 3.135 | 6.004 | 0.435 | 0.493 |
| PIRATES_MAP43 | 61053 | 23 | 0.0% | 0.798 | 2.207 | 3.501 | 68.431 | 0.206 | 1.459 | 2.757 | 0.407 | 0.623 |
| PIRATES_MAP49 | 32260 | 79 | 0.0% | 1.427 | 5.182 | 6.622 | 11.696 | 0.740 | 4.563 | 5.856 | 0.458 | 0.583 |
| PIRATES_MAP50 | 67005 | 32 | 0.0% | 0.716 | 1.848 | 4.440 | 15.232 | 0.128 | 1.295 | 3.814 | 0.349 | 0.536 |
| PIRATES_MAP51 | 69595 | 102 | 0.0% | 0.713 | 2.310 | 2.794 | 10.638 | 0.166 | 1.781 | 2.330 | 0.291 | 0.457 |
| PIRATES_MAP54 | 45196 | 14 | 0.0% | 0.921 | 4.023 | 6.741 | 45.056 | 0.318 | 3.478 | 6.126 | 0.441 | 0.514 |
| PIRATES_MAP57 | 66866 | 51 | 0.0% | 0.736 | 1.841 | 2.403 | 14.540 | 0.130 | 1.265 | 1.757 | 0.360 | 0.520 |
| PIRATES_MAP58 | 65316 | 47 | 0.0% | 0.733 | 2.082 | 4.062 | 14.335 | 0.158 | 1.558 | 3.476 | 0.346 | 0.536 |
| PLANISF_MAP01 | 33754 | 30 | 0.0% | 0.783 | 6.936 | 21.175 | 34.130 | 0.131 | 6.180 | 19.987 | 0.367 | 0.554 |
| SOS_MAP12 | 65773 | 32 | 0.0% | 0.642 | 2.039 | 2.723 | 14.806 | 0.115 | 1.514 | 2.180 | 0.340 | 0.503 |
| SOS_MAP32 | 28086 | 182 | 0.0% | 0.870 | 5.113 | 28.546 | 62.077 | 0.257 | 4.340 | 26.736 | 0.370 | 0.482 |
| SOS_MAP45 | 66759 | 37 | 0.0% | 0.753 | 1.899 | 2.882 | 14.095 | 0.141 | 1.453 | 2.436 | 0.347 | 0.500 |
| SOS_MAP46 | 81935 | 24 | 0.0% | 0.479 | 1.833 | 4.449 | 12.904 | 0.079 | 1.306 | 3.948 | 0.273 | 0.480 |

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
| DOOM2_MAP27_V1 | 102 | 12 | 0.643 | 0.890 | 0.179 | 0.213 | 0.038 | 0.014 | 0.007 | 0.016 | 0.053 | 0.024 | 0.004 | 0.016 | 0.003 | 0.035 | 0.358 |
| DOOM2_MAP31_V1 | 42 | 3 | 0.452 | 0.527 | 0.093 | 0.113 | 0.022 | 0.007 | 0.003 | 0.004 | 0.033 | 0.013 | 0.001 | 0.004 | 0.000 | 0.025 | 0.252 |
| MYHOUSE_20PAM_V1 | 339 | 0 | 0.992 | 1.189 | 0.461 | 0.547 | 0.084 | 0.041 | 0.019 | 0.024 | 0.128 | 0.205 | 0.000 | 0.000 | 0.015 | 0.101 | 0.426 |
| MYHOUSE_MAP01_V1 | 613 | 15 | 6.654 | 9.829 | 5.857 | 6.938 | 0.320 | 0.277 | 0.133 | 0.111 | 0.292 | 0.303 | 0.011 | 0.034 | 0.696 | 0.202 | 0.244 |
| MYHOUSE_MAP01_V2 | 714 | 35 | 6.675 | 9.822 | 5.818 | 6.740 | 0.329 | 0.254 | 0.138 | 0.152 | 0.329 | 0.326 | 0.015 | 0.059 | 0.706 | 0.275 | 0.255 |
| PIRATES_MAP49_V1 | 2022 | 358 | 3.834 | 4.547 | 3.376 | 3.802 | 0.492 | 0.267 | 0.146 | 0.332 | 0.929 | 0.196 | 0.060 | 0.819 | 0.835 | 0.538 | 0.289 |
| PIRATES_MAP54_V1 | 4370 | 1054 | 7.488 | 8.447 | 6.829 | 7.528 | 1.105 | 0.576 | 0.340 | 0.717 | 2.533 | 0.438 | 0.185 | 1.226 | 1.256 | 0.988 | 0.424 |
| PLANISF_MAP01_V1 | 27786 | 1263 | 27.666 | 30.974 | 26.271 | 29.395 | 9.186 | 8.054 | 5.468 | 6.607 | 15.253 | 0.277 | 0.509 | 1.299 | 0.023 | 2.420 | 0.384 |
| PLANISF_MAP01_V2 | 25206 | 1010 | 24.828 | 28.941 | 23.535 | 27.542 | 7.570 | 6.419 | 4.362 | 5.371 | 12.630 | 2.019 | 0.405 | 1.026 | 0.021 | 2.317 | 0.357 |
| SOS_MAP32_V1 | 46176 | 2108 | 49.835 | 53.819 | 47.621 | 51.262 | 15.058 | 12.431 | 8.185 | 7.813 | 19.425 | 10.521 | 0.865 | 2.416 | 0.029 | 5.505 | 0.262 |
| SOS_MAP32_V2 | 19299 | 1195 | 22.899 | 25.910 | 21.220 | 23.930 | 7.370 | 4.927 | 3.159 | 3.561 | 8.762 | 3.555 | 0.430 | 1.271 | 0.035 | 2.125 | 0.228 |

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
| mon_SOS_MAP12_vulkan | 8 | 0.600 | 2.002 | 2.684 | 0.129 | 1.507 | 0.002 | 0.011 |
| mon_SOS_MAP32_gl33 | 10 | 0.740 | 4.417 | 23.751 | 0.141 | 2.994 | 0.003 | 0.003 |
| mon_SOS_MAP32_vulkan | 6 | 0.842 | 5.510 | 30.006 | 0.216 | 4.244 | 0.003 | 0.008 |
| monvant_SOS_MAP32_V1_gl33 | 2514 | 40.859 | 45.656 | 47.370 | 37.023 | 41.249 | 1.036 | 1.767 |
| monvant_SOS_MAP32_V1_vulkan | 2514 | 50.526 | 53.932 | 55.670 | 46.655 | 49.474 | 1.042 | 2.647 |
| monvant_SOS_MAP32_V2_gl33 | 1365 | 18.945 | 21.427 | 22.948 | 16.289 | 18.558 | 0.458 | 0.839 |
| monvant_SOS_MAP32_V2_vulkan | 1365 | 25.087 | 29.106 | 30.192 | 22.207 | 25.742 | 0.531 | 1.466 |
