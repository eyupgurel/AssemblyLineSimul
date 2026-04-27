# Story 18 — Worker visual polish (1.5× scale + idle/walk anim swap)

Approved 2026-04-27.

## Goal

Workers now spawn the UE5 Manny mannequin (Story 17 follow-on), but at default
scale they read as miniature (capsule half-height 90 cm = 180 cm tall, dwarfed
by the 600 cm-wide stations) and they don't visibly walk — the actor slides
along the ground while the mesh stays in T-pose / idle. Two surgical fixes:
bump the actor scale, and swap between idle and walk animations on every
state transition.

## Acceptance Criteria

- **AC18.1** — `AWorkerRobot` spawns at **1.5× actor scale** so the mannequin
  reads at human size against the stations and bucket. Capsule, skeletal
  mesh, and composite parts all scale uniformly via the actor scale.

- **AC18.2** — `AWorkerRobot` plays the engine's `MM_Idle` animation in
  stationary states (`Idle`, `Working`).

- **AC18.3** — `AWorkerRobot` plays `MF_Unarmed_Walk_Fwd` in moving states
  (`MoveToInput`, `MoveToWorkPos`, `MoveToOutput`, `ReturnHome`).

- **AC18.4** — Animation switching happens automatically on every
  `EnterState` transition via a single helper (`RefreshAnimationForState`)
  that reads the current state and calls `SetAnimation` on the
  `SkeletalBodyMesh`. No per-state branching in the FSM cases.

- **AC18.5** — Tests assert:
  - Spawned worker has `GetActorScale3D() == FVector(1.5)`.
  - `PickAnimationForState(Idle)` and `(Working)` return `IdleAnimation`.
  - `PickAnimationForState(MoveToInput / MoveToWorkPos / MoveToOutput / ReturnHome)`
    returns `WalkAnimation`.

## Out of scope

- Walk anim playback rate sync to `MoveSpeed` (feet may slide if the
  walk-anim playback rate doesn't match the worker's velocity; visual
  quirk, not a regression).
- Per-station idle variations.
- Per-mesh skeleton compatibility — only the UE5 mannequin skeleton is
  supported; alternative skeletons would need their own anim assignments.
- `PickUp` and `Place` are transient chain-states (they recurse into a
  moving state immediately), so they aren't asserted in the spec.

## Implementation notes

- `IdleAnimation` and `WalkAnimation` are `UPROPERTY` `TObjectPtr<UAnimSequence>`
  fields loaded once via `ConstructorHelpers::FObjectFinder` from the
  engine-bundled mannequin pack.
- `PickAnimationForState(EWorkerState)` is `const` and pure — public so unit
  tests can assert the contract without requiring an actual `USkeletalMesh`
  on the component (the runtime `GetSingleNodeInstance()` returns null until
  a mesh is assigned).
- `RefreshAnimationForState()` is the side-effecting variant — called from
  `EnterState` after `State` is updated.
- `EnterStateForTesting` exposes `EnterState` to specs so they can drive
  arbitrary transitions without the BeginTask FSM walk.
