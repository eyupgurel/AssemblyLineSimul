# Story 37 — Any DAG terminal completes the cycle

Drafted 2026-05-04. Direct fix for the operator-observed bug:
the 5-station mission `[G → F0 → S → C → F1]` ran end-to-end through
the Checker (mid-chain PASS forwarded to Filter/1 per Story 35), but
when Filter/1 finished with `[82, 76]` the runtime logged
`OnRobotDoneAt: (Kind=1, Inst=1) has no DAG successor` and the cycle
froze — no `OnCycleCompleted` broadcast, no green flash, no auto-loop.

Pre-Story-35 only the Checker could be a terminal, so the runtime
special-cased "Checker has no successors → complete cycle." Story 35
made any node a possible terminal, but didn't lift the matching
completion-handling assumption. Story 37 closes the loop: ANY node
that's a valid DAG terminal triggers cycle completion.

## Story

> As an operator describing a mission whose final stage isn't a
> Checker (e.g., "...check, then take only the best 2"), I want the
> cycle to complete normally when the terminal station finishes —
> green flash, auto-loop, the whole completion beat — instead of the
> bucket sitting silently at the last dock.

## Acceptance Criteria

- **AC37.1 — Any registered node with no DAG successors completes the
  cycle.** When `OnRobotDoneAt(Ref, Bucket)` fires for a non-Checker
  station whose `DAG.GetSuccessors(Ref).Num() == 0` AND `Ref` is a
  registered node (`DAG.FindNode(Ref) != nullptr`), broadcast
  `OnCycleCompleted(Bucket)` and (if `bAutoLoop`) schedule the
  recycle-and-restart timer — same handling as a Checker-terminal PASS.

- **AC37.2 — Checker-terminal behavior unchanged.** The existing
  PASS/REJECT/SendBackTo path for `bIsCheckerTerminal` stays intact
  and runs in preference to AC37.1 when both apply.

- **AC37.3 — Misconfiguration still surfaces a warning.** When
  `OnRobotDoneAt` fires for a `Ref` NOT in the DAG (`FindNode == nullptr`),
  the existing "no DAG successor" warning still fires. The discriminator:
  registered terminal → complete the cycle; unregistered → warn.

- **AC37.4 — Visible PASS cue carries via existing wiring.**
  `AAssemblyLineFeedback` listens to `OnCycleCompleted(Bucket)` and
  flashes a green light at the bucket location. With Story 37 this
  fires for any terminal — Checker, Filter/1, or whatever the
  Orchestrator put at the end. (No "Pass." TTS for non-Checker
  terminals; that's wired in `ACheckerStation::HandleVerdictReply`
  specifically. Future story for any-terminal speech.)

- **AC37.5 — Tests.**
  - `AssemblyLineDirectorSpec`:
    - "OnRobotDoneAt for a non-Checker terminal broadcasts OnCycleCompleted"
      — DAG `[G → F]`; F is terminal; fire `OnRobotDoneAt(F, bucket)`;
      assert OnCycleCompleted broadcast.
    - "OnRobotDoneAt for an unregistered NodeRef still warns" — empty
      DAG, fire `OnRobotDoneAt({Filter, 0}, bucket)`, expect Warning.
    - The existing fan-out / fan-in / cycle-re-entry tests that
      `AddExpectedError("no DAG successor")` are updated: they now
      expect zero warnings (since Sorter at the merge point IS a
      registered terminal) and assert `OnCycleCompleted` instead.

## Out of scope

- **Per-terminal "victory speech."** Non-Checker terminals don't speak
  "Pass." Today the speech is wired in `ACheckerStation::HandleVerdictReply`.
  A future story could surface a configurable per-terminal TTS line.
- **Multi-terminal cycle dedup for auto-loop.** A spec with multiple
  terminals (fan-out where both branches end in distinct terminals)
  fires `OnCycleCompleted` once per terminal that completes. Each
  triggers the auto-loop timer. That schedules multiple `StartCycle`
  calls, which spawn multiple Generator buckets simultaneously —
  not great, but no current mission shape exercises it. Documented
  limitation; future story adds a per-cycle "already completed" flag
  to dedup.
- **`OnCycleCompleted` semantics for fan-out terminals.** Today the
  delegate carries one bucket per fire. With multiple terminals,
  subscribers see multiple fires per "cycle." Defensible (each branch
  is done) but might need a unified-cycle-id story later.

## Implementation notes (non-contract)

- The completion-cycle code in the Checker-terminal branch becomes a
  small helper `CompleteCycle(Bucket)` so AC37.1 can call the same
  path without copying the recycle-timer + auto-loop boilerplate.
- The "valid terminal vs misconfiguration" discriminator uses
  `DAG.FindNode(Ref)` — already public. Returns `nullptr` for an
  unknown Ref.
- The existing `Sorter`-merge-fan-in tests AddExpectedError'd on the
  "no DAG successor" warning because pre-Story-37 the Sorter at the
  merge point logged that warning. Post-Story-37 it broadcasts
  OnCycleCompleted instead. Updated tests assert the broadcast.
