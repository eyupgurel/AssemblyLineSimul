# Story 31c — Fan-out (branching)

Drafted 2026-04-29. Third slice of the DAG executor described in
[Docs/DAG_Architecture.md](../Docs/DAG_Architecture.md). Adds the
"one parent, K children" execution path.

## Story

> As an operator, I want my orchestrator-built mission DAGs to
> support fan-out — one node's output feeding multiple downstream
> stations in parallel — so that I can author missions like
> *"Generator → both Filter-by-primes AND Filter-by-evens in
> parallel, each producing its own bucket for downstream stations."*

## Acceptance Criteria

- **AC31c.1 — `ABucket::CloneIntoWorld`.** Given a bucket with
  `Contents = {3, 7, 11}` and `BilliardBallMaterial` set, when
  `Bucket->CloneIntoWorld(World, Location)` is called, then it
  returns a new `ABucket*` of the same UClass, with `Contents`
  copied, `BilliardBallMaterial` copied, and `RefreshContents()`
  invoked so the clone's visualization is fully built. The original
  bucket's `Contents` is unchanged.

- **AC31c.2 — Fan-out dispatch.** Given a station whose DAG node
  has K > 1 successors, when the worker delivers the bucket and
  `OnRobotDoneAt` fires, then K bucket clones are dispatched (one
  per successor station), each clone has the same `Contents` as the
  source, and the source bucket is destroyed.

- **AC31c.3 — Single-successor preserved.** Given a station whose
  DAG node has exactly 1 successor (the linear-chain case that all
  92 existing specs depend on), when `OnRobotDoneAt` fires, then
  the original bucket is dispatched as-is — no clone, no destroy,
  byte-identical to Story 31b. The 92-spec regression net stays
  green.

- **AC31c.4 — `BucketSpec` test for cloning.** Given a bucket with
  `Contents = {3, 5, 7}`, when `CloneIntoWorld` is called, then the
  returned actor is non-null, distinct from the source, has the
  same `Contents`, and the source `Contents` is untouched.

- **AC31c.5 — `AssemblyLineDirectorSpec` test for 1→2 fan-out.**
  Given a 3-node DAG `A → B, A → C` built with `ATestSyncStation`
  test scaffolds, when the director dispatches from A on a bucket
  with `Contents = {1, 2, 3}`, then both `B` and `C` receive a
  bucket with `Contents = {1, 2, 3}`, both buckets are distinct
  actors, and the source bucket is invalid (destroyed).

- **AC31c.6 — `AssemblyLineDirectorSpec` test for 1→3 fan-out.**
  Same shape as AC31c.5 but with 3 successors; all 3 receive
  distinct clones.

- **AC31c.7 — Regression net.** All 92 specs from Story 31b
  continue to pass, plus the new tests above.

## Out of scope (deferred to later 31x stories)

- **Fan-in / multi-input merge.** Branches that diverge in 31c run
  independently to their own terminals. Story 31d adds the
  multi-input wait-and-collect logic so branches can converge.

- **Worker spawning per branch.** Each station already owns one
  worker (1-per-station model from Story 1). With K successor
  stations, K workers exist naturally — no new spawning logic
  needed in 31c.

- **Cinematic camera adapting to fan-out.** Today's camera director
  has hardcoded shots for 4 stations; branched buckets won't get
  per-branch closeups in 31c. Camera regeneration from spawned
  station positions is a Story 32 concern.

- **Branched `.md` prompt content.** Stations on fan-out branches
  see exactly one input bucket each (one clone per branch), so
  prompts stay single-input. Multi-input prompts are a Story 31d
  concern.

## Implementation notes (non-contract)

- `ABucket::CloneIntoWorld(UWorld* World, FVector Location)`:
  - `World->SpawnActor<ABucket>(GetClass(), Location, ...)`.
  - `Clone->BilliardBallMaterial = BilliardBallMaterial`.
  - `Clone->Contents = Contents` (TArray copy).
  - `Clone->RefreshContents()` so the visualization (balls,
    wireframe, etc.) is built.
  - Returns the clone (or `nullptr` if spawn fails).

- `UAssemblyLineDirector::OnRobotDoneAt` Generator/Filter/Sorter
  branch (the non-Checker dispatch tail):
  - `const TArray<FNodeRef> Successors = DAG.GetSuccessors(...)`.
  - If `Successors.Num() == 1`: today's behavior. Dispatch original.
  - If `Successors.Num() > 1`:
    - For each successor: clone via `Bucket->CloneIntoWorld(...)`,
      dispatch the clone.
    - After all clones dispatched: `Bucket->Destroy()`.
  - If `Successors.Num() == 0`: today's warning (terminal that
    isn't Checker is misconfigured).

- The clone's spawn location: at the source station's `OutputSlot`
  (where the worker just deposited the original). The successor
  station's worker will then traverse from the clone's spawn point
  to its own dock, same as today.

## What stays unchanged

- `AStation::ProcessBucket` signature (still
  `const TArray<ABucket*>& Inputs`, single-element array per call —
  fan-in is 31d).
- `FAssemblyLineDAG` — already had `GetSuccessors` returning
  `TArray<FNodeRef>` from Story 31a. No changes needed.
- `WorkerRobot` FSM — workers don't know about fan-out; they just
  pick up whatever bucket is at their station's input slot.
- All `.md` agent prompts.
- Cinematic, GameMode, Bucket lifecycle for the linear case.

## What this unblocks

After 31c, a custom-built DAG can run a fan-out shape end-to-end:
  - Source generates a bucket.
  - Source's worker delivers to dock; `OnRobotDoneAt` fires.
  - K clones spawn; K successors' workers each pick up their clone.
  - Each branch runs to its own terminal.

Story 31d will add fan-in so branches can converge into a single
multi-input downstream station.
