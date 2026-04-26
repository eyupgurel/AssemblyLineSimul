# Story 16 — Camera Follows the Bucket (REJECT chase + PASS victory beat)

Approved 2026-04-26, extended 2026-04-27 to include the PASS path.

## Goal

Whenever the Checker delivers a verdict, the cinematic switches to a dedicated
**chase camera** that frames the bucket — instead of holding the Checker shot
or snapping back to the wide shot.

- **REJECT** → chase the bucket as the rework worker carries it back to the
  responsible station (Filter or Sorter), then hand off to that station's
  closeup once its worker enters Working.
- **PASS** → chase the bucket through the brief "victory beat" before it's
  destroyed and the next cycle spawns. When the next Generator cycle starts
  (or the bucket vanishes), chase exits and normal shot flow resumes.

The wide-shot flash that briefly appears between Checker `OnStationIdle` and
the chase entry is intentional — it's a visual breather between the closeup
and the chase, and the operator confirmed it reads well.

## Acceptance Criteria

- **AC16.1 — Chase enters on `OnCycleRejected(Bucket)`.** The cinematic flips
  `bChasingBucket = true` and sets `ChaseTarget` to the rejected bucket.

- **AC16.2 — Chase exits when the rework station's worker enters Working.**
  `OnStationActive(StationType)` while chasing turns the chase off and lets the
  closeup-jump take over.

- **AC16.3 — Multi-hop chase target updates.** A second `OnCycleRejected` while
  already chasing updates `ChaseTarget` to the new bucket — chase state
  survives multi-hop rework loops.

- **AC16.4 — Chase enters on `OnCycleCompleted(Bucket)` too (PASS path).**
  Same chase camera, same offset, framing the accepted bucket. Chase ends
  when the bucket is destroyed (Tick observes `ChaseTarget` invalidated and
  jumps to `ResumeShotIndex`) or when the next Generator cycle's
  `OnStationActive` fires.

- **AC16.5 — Null-bucket fallback.** Either broadcast firing with a `nullptr`
  bucket (degenerate path) drops chase state and snaps to `ResumeShotIndex` —
  no chase mode entered.

- **AC16.6 — Chase camera placed immediately on entry.** The chase camera's
  world location and look-at are set BEFORE `SetViewTargetWithBlend`, so the
  first visible frame already frames the bucket — no race with `Tick` that
  would leave the camera at world origin (which happened to frame the
  Generator station, hence the operator's "camera locked on Generator" bug).

- **AC16.7 — Tick re-clamp protects the view target.** While chasing, every
  Tick checks the player's view target and re-asserts the chase camera if a
  stale shot timer or other broadcast has switched it away. Cheap (no-op when
  the view target already matches).

- **AC16.8 — Tests.** Specs on `ACinematicCameraDirector` cover:
  - Chase enters on `OnCycleRejected` with correct target.
  - Chase exits on `OnStationActive` after rework.
  - Second rejection updates the chase target.
  - Chase enters on `OnCycleCompleted` with a valid bucket.
  - Null-bucket on `OnCycleCompleted` falls back to `ResumeShotIndex` and
    does NOT enter chase.

## Out of scope

- Per-shot chase tuning (custom offsets per agent / per chase reason).
- Smoothed chase camera (currently a direct-follow snap; reads as
  "operator panning along the carrier"). Easy to add a lerp later if it
  feels jittery.
