# Story 31a — DAG foundation

Drafted 2026-04-29. First slice of the DAG executor described in
[Docs/DAG_Architecture.md](../Docs/DAG_Architecture.md). Pure
refactor — zero user-visible change.

## Story

> As an operator, I want the assembly line's topology to be built
> from an explicit DAG data structure rather than a hardcoded
> EStationType chain, so that in Story 32 my voice mission can
> dynamically configure a different topology each session — and so
> that Stories 31b–31e (signature change, fan-out, fan-in, GC) can
> layer on without rewriting the dispatch path each time.

## Acceptance Criteria

- **AC31a.1 — Regression net.** Given an assembly line built from a
  4-node linear DAG (`Generator → Filter → Sorter → Checker`), when
  the demo runs a full cycle in PIE, then the cycle completes
  identically to today and all 79 existing specs pass unchanged.

- **AC31a.2 — Cycle rejection.** Given a `BuildFromDAG` call with a
  cyclic spec (e.g. `A → B → A`), when the build runs Kahn's
  algorithm, then the build returns `false` and logs an `Error`
  under `LogAssemblyLine` whose message names the cycle.

- **AC31a.3 — Source / terminal queries.** Given an arbitrary
  in-memory DAG, when `GetSourceNodes()` / `GetTerminalNodes()` are
  called, then they return exactly the nodes with zero parents /
  zero children, in deterministic order.

- **AC31a.4 — Ancestor walk.** Given a node `N` in the DAG, when
  `GetAncestors(N)` is called, then it returns every transitively
  upstream node (excluding `N` itself), with no recursion in the
  implementation (iterative work-queue per the architecture doc's
  "no recursion anywhere" rule).

- **AC31a.5 — Checker derived rule via DAG.** Given the existing
  4-node linear DAG, when `ACheckerStation::GetEffectiveRule()` is
  called, then the composed text is byte-identical to what the
  current hardcoded `Generator + Filter + Sorter` composition
  produces (locked by the existing Checker tests in `StationSpec`).

- **AC31a.6 — Dispatch via DAG.** Given a station completes
  processing in the running line, when the Director picks the next
  station, then the lookup goes through `FAssemblyLineDAG::GetSuccessors`
  on the completing node's `FNodeRef` — not through a hardcoded
  `EStationType` switch.

- **AC31a.7 — New spec file `AssemblyLineDAGSpec`** covers:
  - `BuildFromDAG` succeeds on a valid DAG; succeeds on the empty
    DAG; rejects cycle.
  - `GetSourceNodes` / `GetTerminalNodes` for linear, fork-shaped,
    and merge-shaped fixtures (the latter two not yet exercised by
    production code but the queries must work for Stories 31c/d).
  - `GetAncestors` returns the right set on linear and on a 5-node
    fork-merge fixture.

## Out of scope (deferred to later 31x stories per architecture doc)

- `ProcessBucket` signature change to `TArray<ABucket*>` — Story 31b.
- Bucket cloning + K-worker fan-out dispatch — Story 31c.
- Multi-input fan-in gate (`WaitingFor` / `WaitedOnBy`) — Story 31d.
- Watermark GC, `Store` trait, fluent test builder — Story 31e.
- Cinematic camera dynamic shot regeneration — Story 32.
- Branching / fan-out / fan-in EXECUTION (this story only adds the
  data structures and queries that support them; the dispatch path
  is still single-successor).

## Implementation notes (non-contract)

- New folder `Source/AssemblyLineSimul/DAG/`:
  - `DAGTypes.h` — `FNodeRef` (with `operator<` / `==` / `GetTypeHash`),
    `FStationNode`, `FStationNodeState`. Plain C++ structs (no
    UPROPERTY exposure per MODE 4 U17 / U20 — no Blueprint caller
    yet).
  - `AssemblyLineDAG.h` / `.cpp` — `FAssemblyLineDAG` class. Held by
    value on `UAssemblyLineDirector`. No engine dependencies beyond
    `CoreMinimal.h` and the project's `EStationType` (pure-domain
    layer 1 of the architecture doc; satisfies MODE 1 Clean
    Architecture dependency rule).
- `UAssemblyLineDirector` gains:
  - `FAssemblyLineDAG DAG` (value member).
  - `TMap<FNodeRef, TWeakObjectPtr<AStation>> Stations` registry —
    populated in `RegisterStation` so `GetSuccessors(NodeRef)` →
    actual `AStation*` lookup is one map hop.
  - Existing `RegisterStation` keeps its signature; internally it
    derives the `FNodeRef` from the station's `EStationType` (since
    Story 31a still has 1 instance per kind) and writes both maps.
- `AAssemblyLineGameMode::SpawnAssemblyLine` constructs the 4-node
  DAG once before spawning stations, hands it to the Director via
  a new `Director->BuildLineDAG(TArray<FStationNode>)` call.
- `ACheckerStation::GetEffectiveRule` derived branch swaps the
  hardcoded "Generator + Filter + Sorter" walk for
  `Director->DAG.GetAncestors(CheckerNodeRef)`. For the linear
  4-node DAG the output is byte-identical (regression net = the
  existing Checker tests).
- New `LogAssemblyLine` `Error` line on cycle detection — covered
  by `AddExpectedError` in `AssemblyLineDAGSpec` per MODE 4 U43.

## What stays unchanged

- `ProcessBucket` signature (still `(ABucket* Bucket, FStationProcessComplete)`).
- All four station classes' bodies.
- All `.md` agent prompts.
- The cinematic camera director (still computes shots from `LineOrigin
  + i * StationSpacing`).
- BP_AssemblyLineGameMode wiring.
- The 4-station boot in BeginPlay (still spawns the same 4 actors;
  just expresses their connectivity via the DAG instead of implicit
  `EStationType` ordering).
