# Dynamic sector state mechanism

Type: grilling
Status: open
Blocked by: 01, 04

## Question

Lock the per-sector dynamic-state mechanism (direction already locked at charting: per-sector state buffer +
vertex-shader warp; Doom topology is static, so no runtime re-triangulation):

- Exactly what per-sector dynamic attributes exist: floor/ceiling planes, light levels, colormap/glow/pulse/
  scroll state, runtime texture changes.
- Buffer layout and streaming: full per-frame upload vs dirty-only updates; single dynamic buffer vs
  multiple.
- How the renderer learns which sectors changed: dirty-sector list maintained by `p_spec`? version counters
  on `sector_t`? Where in the existing code do sector changes already get detected?
- Timing vs the game tick and the render thread: when updates are computed, when they reach the GPU.
- Participation of 3D floor models and sector_link target sectors.
- Confirm the exact mechanism against Helion's approach (findings in ticket 01).

End state: a spec-ready section with buffer layouts and the update protocol.
