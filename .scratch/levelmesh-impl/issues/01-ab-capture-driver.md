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

**Status:** ready-for-agent

- [ ] Capture mode default-off: no behavior or perf change when unused; clean, warning-free build.
- [ ] Determinism run zero-diff on all 4 combinations (one acceptance map × {static tick list, demo replay} × {GL33, Vulkan}, cvar 0 on both fresh launches); repeated runs yield byte-identical PNGs.
- [ ] Driver enforces reload-per-path + same-backend pairing; per-run temp autoexec config covers resolution, `vid_preferbackend`, `gl_uselevelmesh`, gamma 1.0, vsync off, default visual cvars.
- [ ] A/B manifest (26 maps, tick lists, the existing 26 synthetic demos) + known-diff allowlist schema committed under `tools/abtest/`.
- [ ] The `gl_uselevelmesh` Bool cvar (`CVAR_ARCHIVE|CVAR_GLOBALCONFIG`, default false) is created here — the per-run config hard-depends on it; path selection lands in 03.

**Note:**
1. Reload-per-path is mandatory: exposure/dither state is monotonic game state (spec §8 A/B note) — one live session cannot host both paths.
2. `gl_uselevelmesh` is created HERE because the driver's per-run config hard-depends on it; its path-selection placement is honored — cvar with the tool (01), selection with the draw (03).
