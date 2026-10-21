# 24: myhouse MAP01 demo A/B: sky / portal regions differ

**Map:** `../map.md`: Acceptance policy.
**Type:** research
**Status:** wontfix
**Blocked by:** —

## Question

The myhouse MAP01 demo A/B (classic vs levelmesh, Vulkan, full lighting; run
`20261016T074457Z-ab`) fails 11 of 87 ticks, five of them over 1M px (ticks 420, 450, 480,
930, 960). The fail set and differing pixels are the same with every 3D-floor surface on the
classic path (`--set-b "lm_3dfloors 0"`, run `20261016T074926Z-ab`), so it is not ticket 12.

What the sheets show (`ab_sheet.py sheet 20261016T074457Z-ab MYHOUSE 420 930 960`):

- tick 420: the levelmesh draws a ceiling across the top of the view where the classic shows
  the walls continuing up (sky-hack ceiling / heightsec / sector portal?);
- ticks 930 / 960: where the classic draws the sky over a courtyard, the levelmesh leaves
  black bands and draws distant geometry the classic hides behind the sky.

Which ticket owns each: sky (14), portals / multi-view (15), heightsecs and hack fills (16),
or the classic leaving geometry out (23)? Start with `--gamestate` and `lm_cullmode 0` to
separate visibility from drawing.

## Comments

- 2026-10-16: ticks 420 / 450 / 480, where a ceiling was drawn across the top of the view,
  were polyobject origin subsectors baked as map-wide flats; fixed in ticket 25. With that
  fix, the myhouse MAP01 A/B fails 5 of 87 (run `20261016T185931Z-ab`). 930 / 960 (sky, over
  1M px) are still open; 1350 / 1440 / 1470 are small (4–642 px).
- 2026-10-20 (wontfix): superseded by .scratch/helion-render (ADR 0004).
