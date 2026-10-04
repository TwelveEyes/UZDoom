# Draw organization & pipeline fit (both backends)

Type: grilling
Status: open
Blocked by: 05, 06, 07

## Question

Lock per-frame draw organization and pipeline integration for both backends (data layer is backend-neutral;
Vulkan first in phasing):

- Draw granularity: one whole-level draw vs per-sector vs chunked/state-grouped draws; state-change
  minimization strategy.
- Fit into the render thread (`r_thread.cpp`) and the Vulkan renderpass/descriptor setup.
- The GL33 draw path design (VAO layouts, state, what the shared data layer exposes to it).
- What replaces the batch system for level geometry, and what stays for sprites (sprites stay on the batch
  path per charting — drawn z-buffered against the mesh).
- The fate of the multithreaded render job pool and texture precache (`hw_precache.cpp`) — e.g. precache
  from the baked mesh's texture list at build time.
- The selection mechanism that makes levelmesh the default once proven: cvar vs ZWidget settings entry.

End state: a spec-ready pipeline section plus a component-fate table (file → kept/replaced/removed for the
geometry path).
