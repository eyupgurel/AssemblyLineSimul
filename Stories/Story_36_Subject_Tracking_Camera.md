# Story 36 — Subject-tracking cinematic camera with framing keyframe sequence

Drafted 2026-05-04. Story 35 made the line spawn for the operator's
5-station mission, but the camera lost track — the second Filter, the
mid-chain Checker, and any future fan-out branches all run "off-camera"
because the cinematic was station-centric (pre-baked fixed shots looked
up by `EStationType`). Replaces the static-shot machinery with a
**subject-tracking** camera that follows whichever bucket is actively
being processed, with a **framing keyframe sequence** (wide → mid →
close → hold) so the audience gets the zoom-in choreography per
processing window, then zooms back out when Working ends.

## Story

> As an operator describing any mission topology — 4-station linear,
> 5-station with two Filters, fan-out, fan-in, mid-chain Checker — I
> want the camera to follow whichever station is currently doing the
> work, zoom in as the bucket is being processed, hold close while the
> agent thinks, and zoom back out when the bucket leaves. The camera
> should never silently miss a station's processing beat just because
> the topology changed.

## Acceptance Criteria

- **AC36.1 — Worker phase events carry `FNodeRef`.** The four
  `FOnWorkerPhase` delegates (`OnPickedUp`, `OnPlaced`,
  `OnStartedWorking`, `OnFinishedWorking`) change from
  `OneParam<EStationType>` to `OneParam<const FNodeRef&>`. Worker
  broadcasts use `AssignedStation->NodeRef` (Story 35 field).

- **AC36.2 — Director re-broadcasts carry `FNodeRef`.**
  `FOnAssemblyLineStationActive` and `FOnAssemblyLineStationIdle`
  rekey from `EStationType` to `FNodeRef`. The Director's
  `RegisterRobot` adapter forwards verbatim.

- **AC36.3 — `CinematicCameraDirector` adopts a subject-tracking
  follow mechanism.** New private state:
  - `ECinematicMode { WideOverview, FollowingBucket, ChasingBucket }`
  - `TWeakObjectPtr<AActor> FollowSubject`
  - `float FollowStartTime`
  - `TObjectPtr<ACameraActor> FollowCamera` (single permanent actor;
    replaces per-station `ShotCameras` for closeups)

- **AC36.4 — `HandleStationActive(FNodeRef)` enters
  `FollowingBucket` mode.** Looks up the worker via
  `Director->GetRobotByNodeRef(Ref)`, gets its current bucket as the
  subject, records `FollowStartTime = World->GetTimeSeconds()`,
  switches mode, and starts ticking the follow camera.

- **AC36.5 — Framing keyframe sequence drives the zoom-in/zoom-out
  choreography.** New `FFramingKeyframe { float Time; FVector Offset;
  float FOV; float BlendTime; }` USTRUCT. New `FFramingSequence`
  USTRUCT wrapping `TArray<FFramingKeyframe>` (so it can be a TMap
  value). New properties on the cinematic actor:
  - `UPROPERTY(EditAnywhere) FFramingSequence DefaultFollowSequence;`
  - `UPROPERTY(EditAnywhere) TMap<EStationType, FFramingSequence> FramingByKind;`
  - Default sequence: t=0 wide-on-bucket, t=2 mid, t=5 close, t=8 hold close.
  - Per-Kind override applies if the entry exists; otherwise default.

- **AC36.6 — While in `FollowingBucket` mode, Tick interpolates the
  active framing.** Each frame:
  - If `FollowSubject` invalidated → switch to `WideOverview` and bail.
  - `Elapsed = World->GetTimeSeconds() - FollowStartTime`
  - Pick the active keyframe (last one with `Time <= Elapsed`).
  - Interpolate `Offset` and `FOV` from the prior keyframe to the active
    one over `BlendTime` (linear interpolation; UE smoothing-curve
    upgrade deferred).
  - Set `FollowCamera->ActorLocation = Subject->GetActorLocation() +
    InterpolatedOffset`; aim at the subject; apply FOV.

- **AC36.7 — `HandleStationIdle(FNodeRef)` returns to
  `WideOverview`.** After `LingerSecondsAfterIdle` (existing field;
  defaults to 0 in tests), blend back to the wide overview shot.

- **AC36.8 — `SpawnCinematicDirector` rewrite.** Stops creating
  per-station closeup shots. Creates exactly:
  - One `FCinematicShot` for the wide overview (at the spawned-line
    centroid, same offset as today).
  - One `ACameraActor` for the wide overview camera.
  - One `FollowCamera` actor (permanent; reused across all follow
    windows + chase events).
  Removes `StationCloseupShotIndex` entries entirely. Removes
  `CheckerShotIndex` (was used by the abandoned early-anticipation
  jump; not used today).

- **AC36.9 — Multi-bucket tiebreak: most-recent subject wins.** When
  a worker enters Working while another worker is already being
  followed, the camera switches to the new subject (matches the
  "the camera is attentive to fresh activity" behavior decided in
  Phase 1).

- **AC36.10 — Chase mechanism unified onto `FollowCamera`.**
  `EnterChase` now reuses the same `FollowCamera` actor (instead of
  spawning a separate `ChaseCamera`). Sets mode to `ChasingBucket`
  with a fixed offset (today's chase offset). `Tick` handles both
  modes via the same follow update; the only difference is whether
  the offset comes from the keyframe interpolation
  (`FollowingBucket`) or a hardcoded chase offset (`ChasingBucket`).

- **AC36.11 — Mid-chain Checker triggers a follow window like any
  other Working state.** No special-casing — when the Checker's
  worker enters Working, camera follows its bucket per the standard
  sequence. Story 35's "no green flash mid-chain" limitation stays
  (Director-level), but the camera at least *frames* the mid-chain
  verdict.

- **AC36.12 — Tests.** The existing `CinematicCameraDirectorSpec`
  tests for `AdvanceShot` / `JumpToShot` / "loops through shot
  indices" / `StationCloseupShotIndex` / closeup-on-idle return
  are replaced wholesale (the static-shot mechanism for closeups
  is gone). New tests:

  `WorkerRobotSpec`:
  - "OnStartedWorking broadcasts the worker's FNodeRef (not just
    Kind)" — set NodeRef on the assigned station, capture and assert.
  - Same for OnFinishedWorking, OnPickedUp, OnPlaced.

  `AssemblyLineDirectorSpec`:
  - The existing "re-broadcasts OnStationActive when worker fires
    OnStartedWorking" test updated to assert on the FNodeRef payload.

  `CinematicCameraDirectorSpec` (replacing the static-shot tests):
  - "HandleStationActive enters FollowingBucket mode with the worker's
    bucket as subject"
  - "HandleStationActive when worker has no bucket → stays in
    current mode (no crash)"
  - "Tick in FollowingBucket mode positions the FollowCamera at
    subject + framing offset"
  - "Tick interpolates between keyframes over time" — set sequence
    `[t=0 offset(0,0,100), t=2 offset(0,0,200)]`, advance world time
    to t=1, assert offset is ~150 (midway).
  - "FollowingBucket mode uses per-Kind override when present"
  - "FollowingBucket mode falls back to DefaultFollowSequence when no
    per-Kind override"
  - "HandleStationIdle returns to WideOverview"
  - "Most-recent-subject tiebreak: second HandleStationActive while
    already following replaces the subject"
  - "FollowSubject invalidated mid-tick (bucket destroyed) → mode
    falls back to WideOverview"
  - "Chase preserved: HandleCycleRejected enters ChasingBucket mode
    with the rejected bucket as subject"
  - "Chase + Follow share the same FollowCamera actor (no double-spawn)"

  `AssemblyLineGameModeSpec`:
  - The existing "SpawnCinematicDirector regenerates Shots from the
    spawned line — 4 stations → 5 shots" test is updated. Post-Story
    36 there's exactly ONE shot (wide overview) regardless of station
    count. Test asserts: 1 shot + 1 wide-overview camera + 1
    permanent FollowCamera actor present after `SpawnCinematicDirector`.

## Out of scope (deferred to follow-ups)

- **Per-NodeRef framing override** (Filter/0 vs Filter/1 distinct
  choreography). The TMap is keyed on EStationType; per-instance is
  a one-line API extension when needed.
- **Smoothing curves** (ease-in/ease-out blending). Linear
  interpolation v1.
- **Manual scripted shots** ("at this exact moment, cut to angle X").
  No existing call site needs this.
- **Per-bucket choreography by mission tag** ("the operator said
  this line is dramatic, use intense angles"). Fun future story.
- **Camera shake / hand-held micro-perturbation** for naturalistic
  feel. Nice-to-have.

## Implementation notes (non-contract)

- `Worker::OnStartedWorking.Broadcast(AssignedStation->NodeRef)`
  replaces `Broadcast(AssignedStation->StationType)` in all four sites
  in `WorkerRobot.cpp`.
- `FOnWorkerPhase` typedef changes from
  `DECLARE_MULTICAST_DELEGATE_OneParam(EStationType)` to
  `DECLARE_MULTICAST_DELEGATE_OneParam(const FNodeRef&)`.
- `Director::OnStationActive`/`OnStationIdle` similarly retypedef.
- `FFramingKeyframe` and `FFramingSequence` go in
  `CinematicCameraDirector.h` (close to their consumer; not in
  AssemblyLineDAG.h since they're camera-specific).
- The wide overview shot keeps using a fixed `ACameraActor` for
  `SetViewTargetWithBlend` (no follow logic needed; subject is
  nullptr).
- Chase offset stays hardcoded at `(-180, 320, 220)` (current value);
  not promoted to a UPROPERTY for v1.
- The `bucket-empty deferral via OnContentsRevealed` complexity in
  the old `HandleStationActive` is gone. With follow mode, the camera
  zooms in on the empty bucket and watches it fill — that's actually
  a nice reveal beat. If it ever feels jarring, we can add a "wait
  for first non-empty Contents" gate to the entry of FollowingBucket.

## What this fixes vs Story 35's documented limitations

Story 35 listed:
- "Cinematic closeups for instance > 0" → **FIXED.** No per-station map; every Working bucket is followed.
- "Per-instance Orchestrator-authored Roles" → **STILL DEFERRED.** Camera doesn't touch this.
- "Per-instance voice/chat routing" → **STILL DEFERRED.** Voice channel separate.
- "Two-tier verdict semantics for mid-chain Checker (visible green flash)" → **STILL DEFERRED.** Director-level concern.
- "Visual disambiguation of two same-kind workers" → **STILL DEFERRED.** Tint/labels are cosmetic; camera now correctly frames each instance.

The "second Filter is invisible to cinematic" specifically called out
in Story 35 Tradeoff 5 is the load-bearing fix here.
