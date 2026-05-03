# Story 31d — Fan-in (multi-input merge)

Drafted 2026-04-29. Fourth slice of the DAG executor described in
[Docs/DAG_Architecture.md](../Docs/DAG_Architecture.md). Adds the
"K parents, one child" execution path — the converse of Story 31c's
fan-out.

## Story

> As an operator, I want my orchestrator-built mission DAGs to
> support fan-in — multiple parent buckets converging into one
> downstream station — so that I can author missions like
> *"Filter-by-primes AND Filter-by-evens, then a merge agent
> combines both kept sets into one bucket for the Sorter."* The
> merge agent's `ProcessBucket` actually receives all K input
> buckets in one call, per the multi-input contract Story 31b set
> up.

## Acceptance Criteria

- **AC31d.1 — Wait-and-collect gate.** Given a DAG node with K > 1
  parents (a fan-in node), when one parent finishes and dispatches
  to it, then the Director queues that parent's bucket and waits.
  When the K-th (last) parent's bucket arrives, the Director fires
  the merge — and only then.

- **AC31d.2 — Merge fires `ProcessBucket` once with all K inputs.**
  Given all K predecessors have delivered to a fan-in node, when
  the merge fires, then the receiving station's `ProcessBucket` is
  invoked **exactly once** with `Inputs.Num() == K` and `Inputs`
  contains all K parent buckets in arrival order.

- **AC31d.3 — Post-merge cleanup.** Given the receiving station's
  `OnComplete` fires after the merge, when the Director continues
  the dispatch chain, then the K−1 secondary input buckets
  (`Inputs[1..K-1]`) are destroyed, and `Inputs[0]` is treated as
  the merged output that continues to downstream stations via
  `OnRobotDoneAt(Child.Kind, Inputs[0])`. (Inputs[0] is the
  primary because it's the first-arrived; it's also the bucket
  the agent is most likely to have written its merged contents
  into. Story 31d codifies a convention; Story 32's prompts
  formalize it.)

- **AC31d.4 — Wait state resets per cycle.** Given a fan-in node
  fired its merge in cycle N, when cycle N+1 begins and parents
  start arriving again, then the wait set re-populates from the
  DAG's parent list — successive cycles fan-in correctly, not just
  the first.

- **AC31d.5 — Single-predecessor unchanged.** Given a node with
  exactly 1 parent (every node in the linear 4-station chain plus
  every fan-out terminal in 31c), when that parent dispatches to
  it, then the Director **does not enter the wait gate** — it
  dispatches immediately, byte-identical to today. The 97-spec
  regression net (Story 31c) stays green.

- **AC31d.6 — Test seam: capture multi-input.**
  `ATestSyncStation` gains a `TArray<TWeakObjectPtr<ABucket>>
  LastInputs` member, populated on each `ProcessBucket` call so
  specs can assert on the inputs passed to a merge.

- **AC31d.7 — 2→1 fan-in test.** `AssemblyLineDirectorSpec` test:
  parents `A`, `B` → child `C`. Trigger A's completion (no fire —
  wait), then B's. Assert `C.ProcessCallCount == 1`,
  `C.LastInputs.Num() == 2`, both buckets present, `Inputs[1]`
  destroyed after merge, `Inputs[0]` survives.

  *3→1 test omitted intentionally:* with only 4 `EStationType`
  values and Checker's special dispatch handling in the Director,
  3 distinct sources + a non-Checker merge target only leaves
  combinations that re-use a kind for both source and merge — which
  would conflict in the `BuildFromDAG` duplicate-NodeRef check. The
  algorithm doesn't distinguish K=2 vs K=3 (loop iterates once more);
  AC31d.8's cycle re-entry case provides the additional coverage.

- **AC31d.8 — Cycle re-entry test.** A test fires the same 2→1
  fan-in twice in sequence (two pairs of parent completions). The
  receiving station's `ProcessCallCount == 2` and the second
  merge's `LastInputs` are the second cycle's buckets, not the
  first.

- **AC31d.9 — All 97 prior specs pass.** The 4-station linear chain
  in `BeginPlay` has no fan-in nodes, so nothing should observably
  change in PIE.

## Out of scope (deferred)

- **Worker visualization of the merge.** Today's worker FSM
  (`AWorkerRobot::BeginTask`) carries one bucket from input slot to
  work pos to output slot. For fan-in, the Director fires
  `Station.ProcessBucket` directly (bypassing `BeginTask`) to keep
  31d small and testable. The visual side — a worker physically
  carrying multiple buckets, or a "merge ceremony" animation — is a
  follow-up story when fan-in actually appears in a mission.

- **`.md` prompt updates for production stations.** The 4 production
  station classes (`AGeneratorStation`, `AFilterStation`,
  `ASorterStation`, `ACheckerStation`) keep their single-input
  `ProcessBucket` bodies (`Bucket = Inputs[0]`). When Story 32's
  orchestrator actually wires a fan-in mission, it can either
  pick a station type and update that station's `.md` prompt
  to reason about multi-input, or introduce a new merge agent
  type. 31d ships the executor capability; agent semantics are
  the orchestrator's problem.

- **`FDAGExecutor` extracted as a separate class.** The wait state
  lives on `UAssemblyLineDirector` for now (consistent with how
  Stories 31a–c added DAG behavior to the Director directly). A
  Story-31e refactor can extract `FDAGExecutor` if the Director
  starts to feel bloated.

- **Worker for the fan-in node fired in the wait state.** Today,
  when the parent's worker delivers to the child's `InputSlot`, the
  child's worker starts its `BeginTask`. For a fan-in node we'd want
  to suppress that until all parents arrived. 31d skips this —
  the worker's BeginTask is bypassed by the direct
  `Station.ProcessBucket` call. Small visual artifact in PIE if a
  fan-in DAG is built; not present in the default linear chain.

## Implementation notes (non-contract)

`UAssemblyLineDirector` adds:

```cpp
// Per-fan-in-child wait state. Re-populated lazily on first arrival
// of each cycle from DAG.GetParents(Child).
TMap<FNodeRef, TSet<FNodeRef>>                WaitingFor;

// Per-fan-in-child inbound bucket buffer. TWeakObjectPtr is GC-safe
// for the brief queue→fire window.
TMap<FNodeRef, TArray<TWeakObjectPtr<ABucket>>> InboundBuckets;

// Helper: called from OnRobotDoneAt's dispatch loop when a successor
// is a fan-in node. Returns true if the bucket was queued (don't
// dispatch normally), false otherwise.
bool QueueForFanInOrDispatch(const FNodeRef& Child, ABucket* Bucket, EStationType ParentType);

// Helper: called when WaitingFor[Child] drains to empty. Fires the
// merge with all queued inputs, schedules cleanup + dispatch chain
// continuation in the OnComplete lambda.
void FireFanInMerge(const FNodeRef& Child);
```

`OnRobotDoneAt`'s dispatch tail (the non-Checker branch) refactors
to:

```cpp
const TArray<FNodeRef> Successors = DAG.GetSuccessors(FNodeRef{Type, 0});
if (Successors.Num() == 0) { ... no-successor warning, return; }

if (Successors.Num() == 1) {
    if (!QueueForFanInOrDispatch(Successors[0], Bucket, Type)) {
        DispatchToStation(Successors[0].Kind, Bucket, GetStation(Type));
    }
    return;
}

// Story 31c fan-out (unchanged), but each clone is now subject to
// the queue-for-fan-in check on its destination.
const FVector CloneSpawnLocation = ...;
for (const FNodeRef& Successor : Successors) {
    ABucket* Clone = Bucket->CloneIntoWorld(GetWorld(), CloneSpawnLocation);
    if (!Clone) continue;
    if (!QueueForFanInOrDispatch(Successor, Clone, Type)) {
        DispatchToStation(Successor.Kind, Clone, GetStation(Type));
    }
}
Bucket->Destroy();
```

`QueueForFanInOrDispatch`:

```cpp
const TArray<FNodeRef> Parents = DAG.GetParents(Child);
if (Parents.Num() <= 1) return false;  // not a fan-in node

TSet<FNodeRef>& Waits = WaitingFor.FindOrAdd(Child);
if (Waits.IsEmpty()) {
    for (const FNodeRef& P : Parents) Waits.Add(P);
}
Waits.Remove(FNodeRef{ParentType, 0});
InboundBuckets.FindOrAdd(Child).Add(Bucket);

if (Waits.IsEmpty()) FireFanInMerge(Child);
return true;
```

`FireFanInMerge`:

```cpp
TArray<TWeakObjectPtr<ABucket>> Weak = InboundBuckets[Child];
TArray<ABucket*> Inputs;
for (const auto& W : Weak) if (ABucket* B = W.Get()) Inputs.Add(B);
InboundBuckets.Remove(Child);
WaitingFor.Remove(Child);  // resets for next cycle

AStation* ChildStation = GetStation(Child.Kind);
if (!ChildStation || Inputs.Num() == 0) return;

ChildStation->ProcessBucket(Inputs,
    FStationProcessComplete::CreateLambda([this, Child, Inputs](FStationProcessResult)
    {
        for (int32 i = 1; i < Inputs.Num(); ++i)
        {
            if (IsValid(Inputs[i])) Inputs[i]->Destroy();
        }
        if (Inputs.Num() > 0 && IsValid(Inputs[0]))
        {
            OnRobotDoneAt(Child.Kind, Inputs[0]);
        }
    }));
```

`ATestSyncStation` (in `Tests/TestStations.h`) gains:

```cpp
TArray<TWeakObjectPtr<ABucket>> LastInputs;

virtual void ProcessBucket(const TArray<ABucket*>& Inputs, FStationProcessComplete OnComplete) override
{
    ++ProcessCallCount;
    LastInputs.Reset();
    for (ABucket* B : Inputs) LastInputs.Add(B);
    FStationProcessResult Result;
    Result.bAccepted = true;
    OnComplete.ExecuteIfBound(Result);
}
```

## What stays unchanged

- `FAssemblyLineDAG` — already had `GetParents` from Story 31a; no
  changes.
- `ABucket` — `CloneIntoWorld` from 31c is reused for branch
  clones; no new bucket lifecycle methods needed for fan-in.
- All four production station classes' `ProcessBucket` bodies —
  still read `Inputs[0]` (no fan-in mission targets them yet).
- All `.md` agent prompts.
- Worker FSM, cinematic camera, GameMode wiring.

## What this unblocks

After 31d, a fan-in DAG (e.g., 2→1 → linear → terminal) runs
end-to-end: parents complete in any order, the merge fires once
with all inputs, and the dispatch chain continues. Combined with
31c, arbitrary tree-shaped DAGs (multi-source / multi-sink with
internal branches and merges) execute correctly.

Story 31e then layers in watermark GC + a `Store` trait + a fluent
test-DAG builder. Story 32 puts the orchestrator on top.
