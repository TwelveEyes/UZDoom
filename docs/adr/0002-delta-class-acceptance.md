# ADR 0002: A/B acceptance with tolerance and delta classes

Status: accepted (2026-10-11)

## Context

The old policy was zero-tolerance pixel diffing with a z-fight-only allowlist. It forced bit-exact
ports and per-vertex shading. It was bent three times anyway: the z-fight allowlist, light
selection, and portal latency. A GPU-driven pipeline adds further legitimate differences:
visibility feedback that arrives late, and exact depth visibility instead of the clipper's
approximation.

## Decision

The A/B comparison pairs Vulkan classic against Vulkan levelmesh, with a fresh level load for each
path. A run passes when every diff pixel either:

- falls within the per-pixel tolerance and cluster-size threshold defined by ticket 03, or
- belongs to a cluster classified under a **delta class**.

Classifications are human-reviewed and recorded in the per-map allowlist JSON. Entries record the
class, map, mode, tick, and cluster geometry.

Initial classes:

- **DC1** z-fight
- **DC2** float-z rounding
- **DC3** feedback latency (bounded by the readback depth)
- **DC4** dynamic-light selection edges
- **DC5** visibility precision, for game-state sets

Adding a class requires amending this ADR.

Geometry and visibility must match exactly wherever they can be tested. Game state (exposure flags,
savegames) is compared by set-diff, with differences classified under DC5.

## Consequences

The design can use approximations that pay for themselves in performance. The cost is
documentation and review work. Unclassified diffs still fail.
