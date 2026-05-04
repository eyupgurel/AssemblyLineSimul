# Story 34 — Re-missioning teardown

Drafted 2026-05-04. Un-defers the "re-missioning mid-session" item
listed as Out of Scope in Story 32b. When a new DAG arrives via the
M key (default mission) or voice (operator-spoken mission), the
previous line — stations, workers, in-flight buckets, cinematic
shots, feedback actor, stale `Saved/Agents/` files, Director state,
and pending timers — is torn down before the new spec spawns.

Driven by the operator-observed bug: after pressing M, then describing
a different mission via voice, the world ended up with two Generators
(the new one + the old, complete with a hanging bucket); when the new
line's first cycle looped back, two buckets appeared at the Generator
dock simultaneously.

## Story

> As an operator, I want to change my mind mid-session and have the
> world reset to match every new mission — no leftover stations, no
> hanging buckets, no stale shots — so the demo stays coherent under
> repeated re-missioning.

## Acceptance Criteria

- **AC34.1 — `AAssemblyLineGameMode::ClearExistingLine()` destroys
  every actor the previous mission spawned.** All non-Orchestrator
  `AStation` actors, all `AWorkerRobot` actors, all `ABucket`
  actors (in-flight and idle), the existing
  `ACinematicCameraDirector`, the existing `AAssemblyLineFeedback`.
  Orchestrator station + `AssemblyLineFloor`-tagged tiles stay.

- **AC34.2 — `UAssemblyLineDirector::ClearLineState()` resets the
  director's bookkeeping.** `StationByType` cleared except for the
  Orchestrator entry; `RobotByStation` reset; `WaitingFor` and
  `InboundBuckets` reset; the held `FAssemblyLineDAG` reset to empty
  (NumNodes == 0). No stale `FNodeRef → AStation*` pointers.

- **AC34.3 — Pending Director-scheduled timers cancelled.** The
  recycle timer (`OnRobotDoneAt` empty-bucket path) and auto-loop
  timer (PASS path) currently use `FTimerDelegate::CreateLambda` and
  aren't trackable. Refactored to `CreateWeakLambda(this, ...)` so
  `World->GetTimerManager().ClearAllTimersForObject(Director)` in
  `ClearLineState` cancels them. Verified by a test that schedules
  a timer, clears, and asserts the timer's side effect doesn't fire.

- **AC34.4 — In-flight worker FSMs and Claude callbacks degrade
  safely.** Existing `TWeakObjectPtr` captures in station
  `ProcessBucket` lambdas already protect against destroyed targets;
  add a smoke test that destroys the bucket mid-cycle and confirms
  no crash.

- **AC34.5 — `HandleDAGProposed` runs `ClearExistingLine` before
  `SpawnLineFromSpec` whenever a prior line exists.** Predicate:
  Director has any non-Orchestrator station registered. First mission
  still works (clear is a no-op on an empty world).

- **AC34.6 — Stale `Saved/Agents/<Kind>.md` files removed before
  the new mission writes its own.** Wipe `Generator.md / Filter.md /
  Sorter.md / Checker.md` (Orchestrator never had one). Otherwise
  a kind present in mission A but absent in mission B would still
  serve mission A's Orchestrator-authored Role.

- **AC34.7 — Orchestrator survives across re-missioning.**
  Orchestrator station registration in Director and the chat history
  in `UAgentChatSubsystem` are preserved (the operator's
  conversation thread continues across missions).

- **AC34.8 — Feedback actor re-spawned post-clear.** `SpawnFeedback`
  is invoked from `HandleDAGProposed` after the spawn so the new
  line has visible PASS/REJECT lights bound to the new Director
  events.

- **AC34.9 — Tests** (deep coverage per operator request):

  `AssemblyLineGameModeSpec` — `ClearExistingLine`:
  - "deletes all non-Orchestrator stations" (4 → 0)
  - "deletes all worker robots" (4 → 0)
  - "deletes all buckets in the world" (3 mock buckets → 0)
  - "deletes the cinematic camera director"
  - "deletes the feedback actor"
  - "preserves the Orchestrator station"
  - "preserves AssemblyLineFloor-tagged static mesh actors"
  - "is a no-op on a fresh boot world (only Orchestrator)"
  - "wipes stale Saved/Agents/<Kind>.md for all four production kinds"

  `AssemblyLineGameModeSpec` — `HandleDAGProposed`:
  - "second invocation leaves only the second line's actors" (4 + 4
    → 3 + Orchestrator, NOT 7 + Orchestrator)
  - "preserves Orchestrator registration in Director across re-mission"
  - "in-flight bucket from prior mission is destroyed by re-mission"
  - "subsequent voice-chat with the new line's agents uses the new
    Saved/Agents/ Role text" (write A's prompts → re-mission with B's
    prompts → load Filter.md → asserts B's prose)

  `AssemblyLineDirectorSpec` — `ClearLineState`:
  - "empties StationByType (except Orchestrator entry)"
  - "empties RobotByStation"
  - "empties WaitingFor and InboundBuckets"
  - "resets the DAG to NumNodes == 0"
  - "cancels active Director-scheduled timers" (proves the
    CreateWeakLambda refactor works — schedule a timer, clear,
    assert side effect doesn't fire after waiting past delay)
  - "preserves the Orchestrator station registration"

## Out of scope

- Visual/animated teardown (pop-out is fine for v1).
- Letting the in-flight cycle finish before swap (atomic teardown
  matches the "I changed my mind, do it now" feel).
- Selective chat-history truncation.
- Tearing down the Floor tiles between missions (they're a
  permanent backdrop).

## Implementation notes

- Order in `HandleDAGProposed`:
  1. `ClearExistingLine` (destroy actors + wipe Saved/Agents/ +
     Director::ClearLineState)
  2. `WriteOrchestratorAuthoredPrompts` (only if PromptsByKind non-empty)
  3. `AgentPromptLibrary::InvalidateCache` (forces re-read)
  4. `SpawnLineFromSpec` (Director::BuildLineDAG + spawn N stations + workers)
  5. `SpawnCinematicDirector` (regen shots)
  6. `SpawnFeedback` (re-bind to Director)
  7. 1.5 s timer → `Director::StartAllSourceCycles`

- Saved/Agents/ wipe deletes all four production kinds unconditionally,
  even if the new mission doesn't use a kind. Guarantees no leakage
  from prior mission's Roles.

- `FAssemblyLineDAG::Reset` already exists privately. Either expose
  publicly or use `DAG = FAssemblyLineDAG{}` assignment in
  ClearLineState. Going with assignment — no API change.

- Worker FSM safety: existing `TWeakObjectPtr` captures handle the
  destroyed-actor case. Smoke test confirms the destroy-mid-cycle
  path doesn't crash.
