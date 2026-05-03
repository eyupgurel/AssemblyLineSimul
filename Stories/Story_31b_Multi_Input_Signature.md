# Story 31b — `ProcessBucket` accepts `TArray<ABucket*>`

Drafted 2026-04-29. Second slice of the DAG executor described in
[Docs/DAG_Architecture.md](../Docs/DAG_Architecture.md). Pure
signature refactor — single-input case behaviorally identical to
today.

## Story

> As an operator, I want my future orchestrator-built mission DAGs
> to support fan-in nodes that receive multiple input buckets, so
> that I can author missions like "Filter by primes AND filter by
> > 50, then merge both into the Sorter" — and so the breaking C++
> change to enable that lands once, in 31b, instead of cascading
> across every later story that touches a station class.

## Acceptance Criteria

- **AC31b.1 — Signature change.** Given the base class and all four
  station subclasses, when their `ProcessBucket` declarations are
  inspected, then the parameter is `const TArray<ABucket*>& Inputs`
  (not `ABucket* Bucket`).

- **AC31b.2 — Single-input behavior preserved.** Given a station
  whose DAG node has exactly one parent (every station today), when
  it processes a bucket via the new signature, then the resulting
  `Bucket->Contents` after `OnComplete` is byte-identical to the
  pre-31b implementation. The 79 pre-31a + 13 31a-added specs (92
  total) pass unchanged.

- **AC31b.3 — Worker dispatch updated.** Given an `AWorkerRobot`
  whose Working state fires `AssignedStation->ProcessBucket(...)`,
  when that call site is inspected, then it passes a one-element
  `TArray<ABucket*>{Bucket}` and the existing FSM continues to work
  (verified by `WorkerRobotSpec` + `FullCycleFunctionalTest`).

- **AC31b.4 — Test scaffolds updated.** Given the test helper
  `ATestSyncStation` and `ATestDeferredStation` in `TestStations.h`,
  when their `ProcessBucket` overrides are inspected, then they
  match the new signature. The specs that drive them
  (`AssemblyLineDirectorSpec`, `WorkerRobotSpec`) pass unchanged.

- **AC31b.5 — Compile-time enforcement of the contract.** Given any
  override or call site of `ProcessBucket` left on the old
  signature, when the project builds, then it fails to compile (the
  override is a `virtual`; mismatched signatures are a hard error).
  This is the regression net for "did we miss a call site."

## Out of scope (deferred to later 31x stories per architecture doc)

- **No `.md` prompt changes.** The agent-facing prompts under
  `Content/Agents/` are untouched in 31b. Today's prompts substitute
  `{{input}}` from the single bucket's `GetContentsString()`; that
  stays. Multi-input prompt content (instructing the agent how to
  reconcile multiple inputs) lands in Story 31d, when the executor
  actually delivers multi-input — it would be premature here.

- **No executor changes.** `FAssemblyLineDAG` and the Director's
  dispatch logic still send exactly one bucket per `ProcessBucket`
  call. Fan-in (multiple parent buckets accumulating into one call)
  is Story 31d.

- **No new tests for multi-input behavior.** With single-input
  semantics and no executor that sends multi-input, there's nothing
  new to assert. The signature change is enforced by the existing
  92 specs continuing to compile + pass.

## Implementation notes (non-contract)

- `Source/AssemblyLineSimul/Station.h`:
  - `virtual void ProcessBucket(const TArray<ABucket*>& Inputs, FStationProcessComplete OnComplete);`
- `Source/AssemblyLineSimul/StationSubclasses.cpp`: four subclass
  overrides updated. Each opens with
  `ABucket* Bucket = (Inputs.Num() > 0) ? Inputs[0] : nullptr;`
  followed by today's body unchanged. The early-out
  `if (!Bucket) return;` already guards the null path.
- `Source/AssemblyLineSimul/WorkerRobot.cpp`: the lambda inside
  `Tick`'s Working-state branch wraps the bucket: `AssignedStation->ProcessBucket(TArray<ABucket*>{Bucket}, ...)`.
- `Source/AssemblyLineSimul/Tests/TestStations.h`: the two test
  station subclasses' `ProcessBucket` overrides updated.

## What stays unchanged

- All `.md` agent prompts.
- `FAssemblyLineDAG` and any DAG queries.
- `UAssemblyLineDirector`'s dispatch path (still one bucket per
  child).
- `OnComplete` callback signature
  (`FStationProcessComplete` / `FStationProcessResult`).
- Bucket lifecycle (no cloning yet — that's 31c).
- Any cinematic / camera / GameMode wiring.
