# 10: Vulkan render worker (async)

**Spec:** `.scratch/levelmesh-rendering/spec.md` §7 (render worker + handoff slots — the
explicit spec-level deviation). Detail:
`.scratch/levelmesh-rendering/ticket-breakup/digest-07-08.md` (the render worker, the
handoff-slot lifecycle, in-order processing, fence-gated back-pressure, the frame-build
placement) + `digest-slices-proposal.md` (slice 10).

**What to build:** The spec-level deviation: a VULKAN-ONLY render worker thread owning the
3D world pass ONLY. Per handoff slot: the uploads (sector-state slot, 3D-light state, level
sky position, window-poly buffer, sprite/model vertex slots), the full 3D issue into the
screen's render target — incl. the occlusion-query issue for the PREDICTED N+1 candidate set
(the worker computes the predicted N+1 camera from the slot's viewpoints: slot viewpoint +
measured velocity × dt, generous frustum) — and a per-slot completion fence. Slot processing
IN ORDER; fence-gated back-pressure (the game thread builds the frame record into the oldest
free slot and blocks on the oldest in-flight slot's fence if none is free); NO frame skipping
— the game thread 2D-composes per frame in order, so every built frame's 3D pass must
complete; stale-frame drop is impossible in this architecture. The game thread keeps the sim,
the frame build (inside the interpolation window), the 2D stack, and presentation; the worker
consumes FROZEN slot contents — never playsim state (and nothing in it may depend on the
validcount-keyed trace-memo state the dither tail touches — A8); the fence wait lands where
today's render-view call happens. The handoff slot = the sector-state ring slot IS the
handoff ring — no second ring (HW_MAX_PIPELINE_BUFFERS slots, the same constant the existing
per-frame buffer-pipeline buffers use). GL33 stays synchronous — no worker, no second GL context. Verified by: A/B Vulkan still zero-tolerance after the
worker lands; in-game 1 h+ stability watch (no crash/hang/GPU memory growth); perf log: the
worker fence-wait window appears and the game-thread CPU render window shrinks vs the
synchronous Vulkan baseline (09).

**Blocked by:** 09 (the worker consumes the same frame record and Vulkan pipelines; it
reshapes nothing in the game-thread frame build).

**Status:** ready-for-agent

- [ ] In-order slot processing, fence-gated back-pressure (game thread builds into the oldest free slot; blocks on the oldest in-flight slot's fence if none free), NO frame skipping; the handoff slot = the sector-state ring slot — no second ring.
- [ ] The worker reads FROZEN slot contents only — never playsim state, never validcount-keyed trace-memo state; the game thread keeps the sim, the frame build (inside the interpolation window), the 2D stack, and presentation; the fence wait lands at today's render-view call site.
- [ ] The worker issues the full 3D pass incl. the occlusion queries for the predicted N+1 candidate set (camera computed from the slot's viewpoints) + a per-slot completion fence.
- [ ] GL33 path untouched — stays synchronous (no worker, no second GL context).
- [ ] A/B Vulkan still zero-tolerance after the worker lands (same-backend pairs only; fresh level load per path).
- [ ] 1 h+ in-game stability watch clean — no crash, no hang, no GPU memory growth.
- [ ] The worker fence-wait window lands in the perf log — the GPU 3D window metric ticket 11's perf protocol measures — and the game-thread CPU render window shrinks vs the synchronous Vulkan baseline (09).

**Note:**
1. This is an explicit spec-level deviation from both Helion (synchronous single-threaded) and the engine's current synchronous HW path, user-chosen: it removes issue + upload CPU cost from the game-thread critical path and is the architectural precondition for deeper async later.
