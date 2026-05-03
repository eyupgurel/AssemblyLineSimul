# Story 32b — Mission-driven boot (Orchestrator-only start, line spawns from spoken mission)

Drafted 2026-05-04. The user-visible payoff for the orchestrator
work begun in Story 32a. Replaces the hardcoded 4-station boot with
an Orchestrator-only start; the operator describes a mission, the
Orchestrator returns a JSON DAG spec, and the line materializes
into the world.

## Story

> As an operator, I want to press Play and see only the Orchestrator
> at the dock, then describe a mission ("generate numbers, filter
> the primes, sort them, then check") and watch the assembly line
> spawn itself and start running — so the demo is mission-driven
> from the first second instead of starting in a fixed configuration.

## Acceptance Criteria

- **AC32b.1 — Boot spawns only the Orchestrator.** After a fresh
  PIE session, the only `AStation` actor in the world is an
  `AOrchestratorStation`. No Generator/Filter/Sorter/Checker exists
  pre-mission. `AAssemblyLineGameMode::SpawnAssemblyLine` is
  removed; replaced by `SpawnOrchestrator` (boot) +
  `SpawnLineFromSpec(const TArray<FStationNode>&)` (mission).

- **AC32b.2 — VoiceSubsystem default-active is Orchestrator.** A
  freshly constructed `UVoiceSubsystem` has
  `HasActiveAgent() == true` and
  `GetActiveAgent() == EStationType::Orchestrator`. The operator's
  first push-to-talk routes to the Orchestrator without needing a
  hail.

- **AC32b.3 — Orchestrator chat reply with `dag` JSON broadcasts a
  parsed spec.** When `AgentChatSubsystem::HandleClaudeResponse`
  receives a successful response addressed to
  `EStationType::Orchestrator` containing a `dag` field, it parses
  via `OrchestratorParser::ParseDAGSpec` and broadcasts a new
  `OnDAGProposed(const TArray<FStationNode>&)` delegate. When `dag`
  is `null` (small-talk), the delegate does NOT fire.

- **AC32b.4 — `SpawnLineFromSpec` spawns one station + one worker
  per node and applies the per-node rule.** Given a 4-node spec
  with one node per kind, the call results in 4 `AStation` actors
  (correct subclass for each `Kind`), 4 `AWorkerRobot` actors
  (each `AssignedStation` set), and each station's `CurrentRule`
  equals the spec's rule for that node. The DAG is registered with
  the `Director` via `BuildLineDAG`. Stations are placed along the
  X axis at `LineOrigin + idx * StationSpacing` in the spec's
  iteration order.

- **AC32b.5 — Director kicks the first cycle on every source node.**
  After `SpawnLineFromSpec` succeeds, `Director::StartAllSourceCycles`
  iterates `DAG.GetSourceNodes()` and dispatches an empty bucket to
  each. (The legacy `Director::StartCycle` becomes a thin wrapper
  that calls `StartAllSourceCycles`, so existing call-sites and
  tests continue to work.)

- **AC32b.6 — `dag: null` does not spawn anything.** When the
  Orchestrator's reply has `"dag": null` (the operator was just
  chatting), no station or worker is spawned and only the
  conversational reply is spoken.

- **AC32b.7 — Bad spec leaves the world untouched.** When the
  parsed spec is invalid (cycle / unknown type / undeclared parent
  / malformed JSON), `SpawnLineFromSpec` returns false, no actors
  are spawned, and the failure is logged at Error. (The Orchestrator
  speaks a graceful "I couldn't build that line" reply via Claude,
  but that's a prompt-engineering concern, not an AC.)

- **AC32b.8 — Cinematic camera regenerates from spawned positions.**
  `SpawnCinematicDirector` runs **after** `SpawnLineFromSpec`, not
  at boot. It iterates the spawned stations (in DAG topological
  order via `Director->GetDAG().GetSourceNodes()` + descendants)
  and rebuilds the `Shots` array from each station's actor
  location. `StationCloseupShotIndex` is repopulated for every kind
  present in the spawned line.

- **AC32b.9 — Single-instance-per-kind constraint, v1.** The
  spawn pipeline supports at most one node per `EStationType` in
  v1 (matches the typical mission). Specs with duplicate kinds are
  rejected by `SpawnLineFromSpec` with an Error log. Multi-instance
  support (and the FNodeRef→Worker map refactor it implies) is
  deferred to a follow-up story — see Out of Scope.

- **AC32b.10 — Regression net.** All 114 specs pass after updating
  the affected ones (see "Tests touched"). New specs cover the
  contract above.

## Tests touched / added

**Updated** (existing specs that asserted on the old boot path):

- `AssemblyLineGameModeSpec`: tests that called `GM->SpawnAssemblyLine()`
  switch to `GM->SpawnLineFromSpec(LegacyFourStationSpec())`. Same
  coverage (worker mesh propagation, BucketClass propagation, floor,
  cinematic) — different entry point.
- `VoiceSubsystemSpec`: "starts with no active agent" → "starts
  with Orchestrator as the default-active agent".

**New**:

- `AssemblyLineGameModeSpec`:
  - "SpawnOrchestrator: world contains exactly one AOrchestratorStation
    and zero other stations"
  - "SpawnLineFromSpec: per-node rule applied to spawned station"
  - "SpawnLineFromSpec: rejects spec with duplicate kinds (logs Error,
    spawns nothing)"
  - "SpawnLineFromSpec on cycle/unknown-type spec leaves world
    untouched"
- `AssemblyLineDirectorSpec`:
  - "StartAllSourceCycles dispatches one bucket per source node"
- `AgentChatSubsystemSpec`:
  - "Orchestrator response with `dag` spec broadcasts OnDAGProposed
    with parsed nodes"
  - "Orchestrator response with `dag: null` does NOT broadcast
    OnDAGProposed"

## Out of scope

- **Multi-instance-per-kind specs** (e.g. two Filters in the same
  line). Requires refactoring `Director::RobotByStation` and
  `StationByType` to be keyed on `FNodeRef` instead of
  `EStationType`, and disambiguating chat/voice routing across
  instances of one kind. Deferred to a follow-up story; v1 enforces
  the single-instance constraint at `SpawnLineFromSpec` (AC32b.9).
- **Re-missioning** mid-session. Operator gets one mission per
  PIE session in v1; relaunch to retry.
- **Persistence** of the spawned topology across sessions.
- **Visual spawn animation** — stations may pop in instantly.
- **UI for previewing the spec before commit** — the operator
  speaks, the line builds.

## Implementation notes (non-contract)

- `AAssemblyLineGameMode::BeginPlay` now calls `SpawnFloor`,
  `SpawnFeedback`, `SetupVoiceInput`, `SpawnOrchestrator`, and
  sets the Voice subsystem's default-active to Orchestrator. It
  does **not** spawn the line or the cinematic director.
- `AgentChatSubsystem` gets an `FOnDAGProposed OnDAGProposed`
  multicast delegate (analog to `OnRuleUpdated`) carrying the
  parsed `TArray<FStationNode>`. `AAssemblyLineGameMode` subscribes
  in `BeginPlay`; the handler calls `SpawnLineFromSpec` and then
  `SpawnCinematicDirector`.
- `SpawnCinematicDirector` iterates the Director's spawned stations
  rather than computing positions from a hardcoded `StationCount`.
  One closeup shot per spawned station; one wide overview shot.
- `Director::StartAllSourceCycles` walks `DAG.GetSourceNodes()` and
  dispatches an empty bucket to each. Replaces the hardcoded
  Generator-only `StartCycle`.
- `VoiceSubsystem::Initialize` overrides the default to set
  `bHasActive = true; ActiveAgent = Orchestrator`. Tests update
  the "initial state" assertion.
