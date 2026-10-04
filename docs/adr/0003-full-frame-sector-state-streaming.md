# The sector state buffer is streamed in full every frame, with no dirty tracking

Every frame, the levelmesh snapshot pass copies **all** sector state records (96 bytes each) into the
current slot of a ring of `HW_MAX_PIPELINE_BUFFERS` GPU buffer slots. No dirty tracking, no per-sector
upload filtering — even when nothing moves.

The buffer is small in absolute terms (~50k sectors × 96 B ≈ 4.5 MB/frame), flicker lights already dirty a
large fraction of sectors on most ticks, and a byte-identical buffer on every run makes the A/B acceptance
tool (ticket 09) deterministic and trivially diffable. The ring slots exist to keep full copying safe with
multi-frame pipeline depth (the CPU only ever touches the current slot; rare re-bake patches go to all
slots), not to optimize.

Considered: **Helion's dirty-only streaming**, which uploads only the words of sectors that changed
(`research/01-helion-levelmesh.md`). Rejected: at Doom scale the bandwidth saving is immaterial while the
complexity and the run-to-run nondeterminism are real; a future "optimization" back to dirty tracking is
possible but would give up the determinism this ADR buys.

Consequence: the snapshot pass is a flat pack of every record plus a record diff — but the diff exists only
to drive the static/dynamic lifecycle (first movement flips a surface dynamic; two settled frames re-bake),
not to decide what gets uploaded.
