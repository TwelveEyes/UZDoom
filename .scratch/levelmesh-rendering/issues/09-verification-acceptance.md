# Verification & acceptance plan

Type: grilling
Status: open
Blocked by: 08, 11

## Question

Lock the acceptance plan (direction locked at charting: deterministic A/B rendering tooling is in scope):

- Acceptance map list: name concrete maps/WADs covering vanilla, complex ports, portals/sector_link, 3D
  floors, and slaughter scale.
- The A/B tool design: how it drives the engine (same map, same camera, both paths), what it captures,
  pixel-diff tolerances, where it lives in the tree, how it runs headless or in-game.
- Profiling targets: the frame-time budget the levelmesh path must meet or beat (the classic-path baseline
  is produced by ticket 10).
- The precise definition of "proven" that gates flipping the default to levelmesh.
- The in-game soak checklist.

End state: a spec-ready verification section.
