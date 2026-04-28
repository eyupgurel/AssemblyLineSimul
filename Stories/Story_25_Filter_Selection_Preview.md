# Story 25 — Filter selection preview (highlight kept balls for 1 s before dropping the rest)

Drafted 2026-04-28. Supersedes Story 24's mechanism.

## Goal

Tell the Filter's selection story visually:

1. Bucket arrives at Filter dock with all input balls visible.
2. Filter calls Claude; Claude returns the kept subset.
3. The SELECTED balls glow emissive gold while the REJECTED balls
   stay with their normal painted-number material — audience sees
   "these are the ones the Filter picked."
4. After 1 second, the rejected balls vanish; only the survivors
   remain.
5. Worker carries the surviving subset to Sorter.

This supersedes Story 24, whose `ApplyGoldEmissiveToBalls` painted
every ball in the post-filter bucket — but by that point the
rejected balls had already been destroyed by `RefreshContents`, so
the visual was "all visible balls glow," with no contrast to
distinguish selected from rejected.

## Acceptance Criteria

- **AC25.1** — Story 24 cleanup:
  - `ABucket::ApplyGoldEmissiveToBalls()` removed from `Bucket.h/cpp`.
  - The Story 24 caller in `AFilterStation`'s LLM response handler
    removed.
  - The `BucketSpec.cpp` test
    `"swaps every ball's material to a MID of EmissiveMeshMaterial"`
    removed.

- **AC25.2** — `ABucket` gains
  `void HighlightBallsAtIndices(const TArray<int32>& Indices)`.
  For each ball whose index is in `Indices`, swaps material slot 0
  to a `UMaterialInstanceDynamic` of
  `/Engine/EngineMaterials/EmissiveMeshMaterial` with `Color` set
  to `GlassTint`. Balls not in `Indices` are untouched.

- **AC25.3** — `AFilterStation` gains a static helper
  `static TArray<int32> FindKeptIndices(const TArray<int32>& InputContents, const TArray<int32>& KeptValues)`.
  For each value in `KeptValues` (in order), finds its first matching
  index in `InputContents` that hasn't already been claimed; returns
  the matched indices. Handles duplicates (e.g. input `[3, 5, 7, 5]`
  + kept `[5, 7]` → indices `[1, 2]`).

- **AC25.4** — `AFilterStation`'s LLM response handler is restructured:
  1. Parses kept `Numbers`.
  2. Computes `KeptIndices = FindKeptIndices(B->Contents, Numbers)`.
  3. Calls `B->HighlightBallsAtIndices(KeptIndices)` — selected balls
     glow gold; rejected stay with their normal material.
  4. Sets a 1-second timer via the world's `TimerManager`.
  5. On timer expiration: `B->Contents = MoveTemp(Numbers);
     B->RefreshContents(); OnComplete.ExecuteIfBound(R);` — rejected
     vanish, survivors remain, worker proceeds.

- **AC25.5** — Tests:
  - `BucketSpec`: with `Contents = {3, 5, 7}` and after
    `HighlightBallsAtIndices({0, 2})`:
    - balls 0 and 2 have a MID with parent EmissiveMeshMaterial,
    - ball 1's material is NOT that MID.
  - `BucketSpec`: with `Indices = {}` (empty), no ball is highlighted.
  - New `StationSubclassesSpec.cpp` (or wherever fits) tests
    `AFilterStation::FindKeptIndices`:
    - `({1,2,3}, {2,3})` → `{1,2}`.
    - `({3,5,7,5}, {5,7})` → `{1,2}` (first occurrences).
    - `({}, {1})` → `{}` (empty input).
    - `({1,2,3}, {})` → `{}` (empty kept).

- **AC25.6** — All existing specs (those not removed in AC25.1) still pass.

## Out of scope

- Configurable preview duration — hardcoded 1.0 second.
- Animated fade in / fade out on the highlight (binary material swap).
- Particle/sound effect when rejected balls vanish (they just disappear).
- Highlighting rejected balls separately (e.g. red glow for rejects).
