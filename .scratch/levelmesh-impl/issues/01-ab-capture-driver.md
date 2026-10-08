# 01: A/B capture mode + pixel-diff driver

**Spec:** `.scratch/levelmesh-rendering/spec.md` §9 (verification & acceptance — decisions D1–D12, run
structure, acceptance matrix, allowlist policy) + §2 glossary (A/B capture mode, acceptance matrix,
known-diff allowlist). Detail: `.scratch/levelmesh-rendering/ticket-breakup/digest-09-11.md` and
`digest-slices-proposal.md` (slice 01).

**What to build:** An always-compiled, cvar-gated A/B capture mode (default off) plus an external driver
that measures classic-vs-levelmesh pixel parity at zero tolerance. In capture mode the engine runs the sim
on a synthetic clock — exactly one tick per rendered frame, interpolation fraction pinned at 0.5, sky
scroll / `TexAnim` / camera FOV computed from tick time (wall-clock `I_GetTimeFrac()` is untrustable even
under `-timedemo`/`cl_capfps`) — and writes the FINAL COMPOSED framebuffer (post-2D: 3D world + HUD +
overlays) as per-tick PNGs via the existing `M_ScreenShot` PNG writer. `tools/abtest.py` (Python 3 +
Pillow + stdlib) drives run pairs: per-run temp autoexec config (resolution, `vid_preferbackend`,
`gl_uselevelmesh`, gamma 1.0, vsync off, default visual cvars), a FRESH LEVEL LOAD PER PATH, and
same-backend pairs only (classic-GL33 vs levelmesh-GL33, classic-VK vs levelmesh-VK). The comparator is
zero tolerance: per-pixel diff of the PNG pair, connected-component diff clusters, any pixel outside the
allowlist fails the run. Also committed with the driver: the A/B manifest (the 26 acceptance maps,
per-map tick lists, the existing 26 synthetic demos in `tools/abtest/demos/`) and the known-diff allowlist
schema under `tools/abtest/allowlists/` (z-fighting only, human-reviewed, shrink-only — ADR 0004). The
tool proves itself with a classic-vs-classic determinism run before any levelmesh work is measured against
it: one acceptance map × {static tick list, demo replay} × {GL33, Vulkan}, cvar 0 on both fresh launches —
zero diff, and repeated runs yield byte-identical PNGs.

**Blocked by:** None (can start immediately)

**Status:** done (all boxes checked; determinism bar proven 2026-10-08)

- [x] Capture mode default-off: no behavior or perf change when unused; clean, warning-free build.
- [x] Determinism run zero-diff on all 4 combinations (one acceptance map × {static tick list, demo replay} × {GL33, Vulkan}, cvar 0 on both fresh launches); repeated runs yield byte-identical PNGs.
- [x] Driver enforces reload-per-path + same-backend pairing; per-run temp autoexec config covers resolution, `vid_preferbackend`, `gl_uselevelmesh`, gamma 1.0, vsync off, default visual cvars.
- [x] A/B manifest (26 maps, tick lists, the existing 26 synthetic demos) + known-diff allowlist schema committed under `tools/abtest/`.
- [x] The `gl_uselevelmesh` Bool cvar (`CVAR_ARCHIVE|CVAR_GLOBALCONFIG`, default false) is created here — the per-run config hard-depends on it; path selection lands in 03.

**Note:**
1. Reload-per-path is mandatory: exposure/dither state is monotonic game state (spec §8 A/B note) — one live session cannot host both paths.
2. `gl_uselevelmesh` is created HERE because the driver's per-run config hard-depends on it; its path-selection placement is honored — cvar with the tool (01), selection with the draw (03).

## Comments

- **2026-10-08 — in progress; implementation committed, determinism bar unverified.**
  All of the build deliverable is committed on `agent-levelmesh` (commits `4f63f88`
  capture mode + driver, `8e9f9ca` oversized-cluster crash fix, `45b2ba4` Vulkan
  present-clipping fix at HEAD):
  - A/B capture mode `r_ab_capture` (default off) — `src/d_abcapture.cpp/.h`,
    synthetic clock in `i_time.cpp`, 1-tick-per-frame pacing in `d_net.cpp`,
    render-pass hooks in `d_main.cpp`, Vulkan composed-frame hook in
    `vk_renderdevice.cpp`.
  - `tools/abtest.py` driver (`run --determinism` / `diff` / `record` / `validate`),
    per-run fresh autoexec, reload-per-path (one process per path), same-backend
    pairing, zero-tolerance comparator + connected-component clusters + allowlist
    coverage.
  - `tools/abtest/ab_manifest.json` (26 maps) + 26 `demos/*.lmp` +
    `tools/abtest/allowlists/README.md` schema.
  - `gl_uselevelmesh` Bool cvar in `hw_cvars.cpp` (default false).
  - Per-run config pins verified by inspection: resolution 2560×1440, `vid_preferbackend`,
    `gl_uselevelmesh`, `vid_fixgamma 0` (gamma 1.0), `vid_vsync 0`,
    `vid_contrast 1`/`vid_saturation 1`.

  **Still open (gates resolution):**
  - **Determinism bar (box 2) is not proven.** No `results.json` committed; `tools/abtest/runs/`
    is empty. The `8e9f9ca` commit documents a ~3M-px whole-frame divergence on the
    demo-replay path in the matrix — i.e. the classic-vs-classic demo-replay combo is not
    yet frame-identical. This is a real determinism bug to chase before any levelmesh path
    is measured against the tool.
  - **Clean, warning-free build (box 1) not re-verified.** `build/uzdoom` predates HEAD by
    ~10 min (missing the `45b2ba4` Vulkan fix, which is on the Vulkan determinism path). Needs
    a fresh `cmake --build build --config RelWithDebInfo --parallel 3`.
  - Box 3 (driver enforcement) is implemented and code-verified, but its end-to-end proof
    comes with the determinism run, so it is left unchecked for now.

  **Next:** fresh build → `python3 tools/abtest.py run --determinism` (real GPU, unsandboxed)
  on all 4 combos → chase the demo-replay divergence to zero → commit the `results.json`
  evidence + close the boxes.

- **2026-10-08 — resolved; determinism bar proven, all boxes closed.**
  The demo-replay divergence was caused by un-locked input during capture. `System_DispatchEvent`
  in `d_main.cpp` processes `EV_Mouse` events directly (calling `G_AddViewPitch`/`G_AddViewAngle`)
  before the event reaches the queue, bypassing the `D_ProcessEvents` early return. Fixed by adding
  an `AbCaptureEnabled` guard in `System_DispatchEvent` to drop `EV_Mouse` events during capture.
  All 4 determinism combos now pass with byte-identical PNGs:
  - `DOOM2_MAP01_static_gl33`: 30/30 PASS
  - `DOOM2_MAP01_static_vulkan`: 30/30 PASS
  - `DOOM2_MAP01_demo_gl33`: 87/87 PASS
  - `DOOM2_MAP01_demo_vulkan`: 87/87 PASS
  Results: `tools/abtest/runs/20261008T210017Z-det/results.json`.
