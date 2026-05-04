# Story 35 — Multi-instance per Kind support

Drafted 2026-05-04. Un-defers the "single-instance per Kind" v1
constraint that Story 32b explicitly listed as Out of Scope
(AC32b.9). Direct response to the operator-observed 5-station
mission bug:

> *"Generate 20, take only evens, sort descending, check, then take only the best 2"*
> ↳ in one case the line never spawned ("did nothing" — duplicate-kind
>    rejection); in another the bucket reached the Checker mid-chain and
>    never forwarded ("got stuck" — Checker assumed terminal).

Both root causes are deferrals from Story 32b. Story 35 lifts both
together so 5+ station missions with two Filters / two Sorters /
mid-chain Checkers spawn and run end-to-end.

## Story

> As an operator describing a mission with more than four logical
> stages — or a mission that needs two of the same agent kind doing
> different jobs — I want the line to spawn the full topology and
> run end-to-end, not silently fail or stall mid-chain.

## Acceptance Criteria

- **AC35.1 — `SpawnLineFromSpec` accepts multi-instance specs.** The
  duplicate-kind rejection (Story 32b AC32b.9) is removed. A spec
  with `[G:0, F:0, S:0, C:0, F:1]` spawns 5 stations and 5 workers.
  Each station is a distinct actor; each worker is bound to one
  specific station.

- **AC35.2 — Director maps rekey to `FNodeRef`.**
  - `StationByType : TMap<EStationType, AStation*>` →
    `StationByNodeRef : TMap<FNodeRef, AStation*>`.
  - `RobotByStation : TMap<EStationType, AWorkerRobot*>` →
    `RobotByNodeRef : TMap<FNodeRef, AWorkerRobot*>`.
  - Public `GetStationOfType(EStationType T)` and
    `GetRobotForStation(EStationType T)` continue to exist as
    backward-compat shims that return the **Instance 0** match
    for that Kind (so chat / voice / cinematic for "Hey Filter"
    still work in the typical 4-station mission).
  - New `GetStationByNodeRef(FNodeRef)` and
    `GetRobotByNodeRef(FNodeRef)` are the canonical lookups.

- **AC35.3 — `AStation::NodeRef` field.** New `FNodeRef NodeRef`
  member set by `RegisterStation` (auto-instance on register: per-Kind
  counter on the Director, back-written into the station). Existing
  callers that register only one of each Kind continue to work
  unchanged (Sorter → Sorter/0, etc.). `ClearLineState` resets the
  counter.

- **AC35.4 — `OnRobotDoneAt(FNodeRef, ABucket*)` is the canonical
  entry.** The existing `OnRobotDoneAt(EStationType, ABucket*)`
  becomes a thin shim that calls the FNodeRef form with `{Type, 0}`
  — preserves backward-compat for the existing fan-out / fan-in /
  recycle / StartCycle tests that pass EStationType. Internal
  dispatch (`DispatchToStation`, `QueueForFanInOrDispatch`,
  `FireFanInMerge`) operates on `FNodeRef` end-to-end so multi-instance
  flows correctly.

- **AC35.5 — Worker callback captures `FNodeRef`.** The lambda fed
  into `Robot->BeginTask(...)` records the dispatch's `FNodeRef`
  (not the kind alone) and feeds it to `OnRobotDoneAt(FNodeRef, ...)`.
  So when Filter/1 finishes, the Director knows to consult
  `DAG.GetSuccessors(FNodeRef{Filter, 1})`, not Filter/0's successors.

- **AC35.6 — Checker handles non-terminal placement.** When a Checker
  completes, the Director consults `DAG.GetSuccessors(CheckerNodeRef)`:
  - **No successors (terminal — typical 4-station case):** Existing
    behavior. PASS broadcasts `OnCycleCompleted` and auto-loops; REJECT
    sends back via `SendBackTo`.
  - **Has successors (mid-chain):** PASS forwards the bucket to the
    successor via the standard dispatch path (no `OnCycleCompleted`,
    no auto-loop). REJECT still sends back via `SendBackTo`. Documented
    limitation: no green flash mid-chain (see Tradeoff 2 in the
    Phase 1 design notes).

- **AC35.7 — `ClearLineState` clears the new maps.** Resets
  `StationByNodeRef` (preserving the Orchestrator's NodeRef entry)
  and `RobotByNodeRef`. Resets the per-Kind instance counter so the
  next mission starts fresh.

- **AC35.8 — Tests** (the load-bearing test is the integration one
  at the end):

  `OrchestratorParserSpec`:
  - "parses a 5-node spec with two Filters into FNodeRef{Filter,0} +
    FNodeRef{Filter,1}" (parser already supports it; explicit assertion).

  `AssemblyLineGameModeSpec`:
  - "SpawnLineFromSpec accepts a spec with two Filters and spawns
    5 stations + 5 workers" (the AC32b.9 rejection is gone).
  - "Each spawned station's `NodeRef` matches its spec node's NodeRef".
  - "GetStationOfType(Filter) returns the Instance 0 station
    (backward-compat shim) when two Filters exist".

  `AssemblyLineDirectorSpec`:
  - "RegisterStation auto-assigns FNodeRef using the per-Kind
    counter (two Filters get Filter/0 and Filter/1)".
  - "StationByNodeRef holds both Filter instances; one doesn't
    overwrite the other".
  - "GetStationByNodeRef(Filter/1) returns the second Filter
    distinct from GetStationByNodeRef(Filter/0)".
  - "OnRobotDoneAt(FNodeRef{Filter,0}) dispatches to Filter/0's
    successor; OnRobotDoneAt(FNodeRef{Filter,1}) dispatches to
    Filter/1's successor (different from Filter/0's)".
  - "ClearLineState empties StationByNodeRef + RobotByNodeRef
    (preserves Orchestrator) + resets per-Kind counter".
  - "Checker terminal: PASS broadcasts OnCycleCompleted (existing)".
  - "Checker mid-chain: PASS dispatches to successor (no
    OnCycleCompleted, no auto-loop)".
  - "Checker mid-chain: REJECT routes via SendBackTo (existing)".

  **End-to-end integration test (the operator's exact scenario):**
  - "5-station mission [Generator → Filter/0 → Sorter → Checker →
    Filter/1] spawns 5 stations + 5 workers; dispatch reaches all
    5 instances in DAG order; terminal Filter/1's ProcessCallCount
    == 1 after one cycle".

## Out of scope (deferred to follow-up stories)

- **Per-instance voice/chat routing** — "Hey Filter Two".
  Limitation: voice and chat-driven rule edits route to Instance 0.
- **Per-instance Orchestrator-authored Roles** — both Filter/0 and
  Filter/1 share the same `Saved/Agents/Filter.md` Role. Their
  `CurrentRule` differs (set per-node during spawn), but the
  personality prose is shared.
- **Cinematic closeups for instance > 0** — `SpawnCinematicDirector`'s
  `SeenKinds : TSet<EStationType>` collapses multi-instance into one
  closeup per Kind. Filter/1 has no closeup; the camera doesn't jump
  when its worker enters Working. Documented limitation.
- **Two-tier verdict semantics for mid-chain Checkers** (visible
  intermediate verdict). Mid-chain PASS is silent; only the actual
  terminal completion fires `OnCycleCompleted`.
- **Visual disambiguation of two same-kind workers** (per-instance
  tint variation, name labels). Cosmetic.

## Implementation notes (non-contract)

- `RegisterStation(AStation*)` keeps its existing signature. The
  per-Kind counter lives on the Director:
  `TMap<EStationType, int32> NextInstanceByKind`. Each register
  bumps the counter; the assigned `FNodeRef` is back-written to
  `Station->NodeRef`. ClearLineState resets the counter.

- `RegisterRobot(AWorkerRobot*)` keys on the robot's
  `AssignedStation->NodeRef` (not StationType).

- `GetStationOfType(EStationType T)` is implemented as
  `GetStationByNodeRef(FNodeRef{T, 0})` — Instance 0 by convention.

- `OnRobotDoneAt(EStationType T, ABucket* B)` is implemented as
  `OnRobotDoneAt(FNodeRef{T, 0}, B)` for backward compat with tests
  that exercise the old API.

- `DispatchToStation(FNodeRef Target, ABucket* Bucket, AStation*
  SourceStation)` is the canonical signature. The worker callback
  captures `Target` and feeds it to `OnRobotDoneAt(Target, ...)` on
  completion.

- Checker mid-chain detection: in `OnRobotDoneAt`, when
  `Ref.Kind == EStationType::Checker`, check
  `DAG.GetSuccessors(Ref).Num() > 0` before short-circuiting to the
  PASS/REJECT terminal branch. With successors, PASS falls through
  to the standard dispatch.
