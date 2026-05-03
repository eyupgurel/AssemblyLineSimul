# Story 32a — Orchestrator agent type + DAG-spec parser

Drafted 2026-04-29. First slice of the orchestrator work described
in [Docs/DAG_Architecture.md](../Docs/DAG_Architecture.md). Adds the
agent type, its prompt, and the JSON-spec parser. **Current 4-station
boot is unchanged — Story 32b makes the user-visible switch.**

## Why split

Story 32 ships the user-visible payoff (talk to the orchestrator at
start, watch it build the line). It needs:
- A new agent type (chat target, prompt, parser).
- A new boot path (orchestrator alone at start; spawn the rest on
  mission).
- Cinematic camera regenerated from spawned positions.

Doing all of that in one story produces a 600-line diff with one
manual PIE check at the end. Splitting into two:
- **32a (this story):** agent + prompt + parser. Self-contained;
  existing demo runs identically. Tests prove the parser works in
  isolation.
- **32b (next):** boot replacement. Removes hardcoded
  `SpawnAssemblyLine`; spawns only the orchestrator at start;
  mission → spawn rest → first cycle. Cinematic camera adapts.

PIE check after 32a confirms nothing regressed; PIE check after
32b is the actual demo experience.

## Story

> As an operator, I want to be able to address an Orchestrator
> agent (`EStationType::Orchestrator`) and have it return a
> structured DAG spec for whatever mission I describe — without
> yet replacing the boot — so that the prompt + parser contract
> is validated in isolation before Story 32b uses it to replace
> the 4-station boot.

## Acceptance Criteria

- **AC32a.1 — `EStationType::Orchestrator` enum value.** Added to
  `EStationType` in `AssemblyLineTypes.h`. Existing enum users
  (chat subsystem switch tables, `Director::GetStationOfType`,
  etc.) keep compiling without behavior change for the existing 4
  stations.

- **AC32a.2 — `Content/Agents/Orchestrator.md`.** New agent
  prompt file with sections `## DefaultRule`, `## Role`, and
  `## ProcessBucketPrompt` (the last unused — orchestrator never
  processes buckets — but conforms to the per-agent .md schema).
  The Role describes the orchestrator's job; the DefaultRule
  is "Listen to the operator's mission and emit a DAG spec." The
  prompt is loaded via `AgentPromptLibrary::LoadAgentSection`
  like the other 4 agents.

- **AC32a.3 — Orchestrator chat prompt template.** The
  `ChatPrompt.md` template the chat subsystem uses for every agent
  asks for `{"reply": "...", "new_rule": "..." | null}`. For the
  orchestrator we need a different schema:
  `{"reply": "...", "dag": {"nodes": [...]} | null}`. Two ways to
  handle this:
  - **A.** A new section `## OrchestratorChatPromptTemplate` in
    `ChatPrompt.md` that the chat subsystem uses **only when
    StationType == Orchestrator**. Cleanest separation.
  - **B.** Single template that mentions both schemas; the
    orchestrator's prompt and other agents' prompts are
    differentiated by their per-agent Role text.
  - **AC chooses A.** Cleaner contract; the chat subsystem reads
    the orchestrator-specific section when the target is the
    orchestrator. Single-responsibility per template section.

- **AC32a.4 — `FAssemblyLineDAGSpec` parser.** New free function
  `bool ParseOrchestratorDAGSpec(const FString& JsonText,
   TArray<FStationNode>& OutNodes)` under
  `Source/AssemblyLineSimul/DAG/OrchestratorParser.{h,cpp}`.
  Accepts the JSON the orchestrator returns; populates
  `OutNodes`; returns false on malformed JSON or invalid spec
  (unknown EStationType, duplicate ID, etc.). Logs a categorized
  error on failure under a new `LogOrchestrator` category.

- **AC32a.5 — Parser test coverage.** New `OrchestratorParserSpec`
  covers:
  - Empty `{"nodes": []}` returns true with empty array.
  - Linear `{"nodes": [{"id":"gen","type":"Generator","rule":"..."},
    {"id":"flt","type":"Filter","rule":"...","parents":["gen"]}, ...]}`
    parses to the expected `TArray<FStationNode>`.
  - Fan-out (one source, two children) parses correctly.
  - Fan-in (two sources, one merge child) parses correctly.
  - Malformed JSON returns false + logs `Error`.
  - Unknown station type (`"type":"FooBar"`) returns false + logs
    `Error`.
  - Reference to undeclared parent ID returns false + logs `Error`.

- **AC32a.6 — Orchestrator station class.** `AOrchestratorStation
  : AStation` (in `StationSubclasses.h/cpp` alongside the other
  4) with constructor that sets `StationType = Orchestrator` and
  loads `DefaultRule` from `Orchestrator.md`. `ProcessBucket`
  inherits the AStation default (which is a no-op accepting
  result) — orchestrator never appears in the line's DAG, so this
  is unreachable.

- **AC32a.7 — Regression net.** All 107 specs pass unchanged. The
  4-station boot still runs identically; no PIE behavior change.
  Changes are purely additive: a new enum value (untouched by
  existing code paths), a new station subclass (unspawned), a
  new .md file (unread by anything pre-32b), a new parser (only
  exercised by tests).

## Out of scope (deferred to Story 32b)

- **Replacing the hardcoded `SpawnAssemblyLine`** with
  orchestrator-driven boot. This story doesn't touch
  `AAssemblyLineGameMode::BeginPlay`.
- **Sending real missions to Claude through the orchestrator.**
  The chat subsystem's orchestrator-aware path lands in 32b along
  with the spawn pipeline.
- **Cinematic camera regen** from spawned positions.
- **Spawning stations from a DAG spec at runtime.** The parser
  produces `TArray<FStationNode>`; the spawn step consumes that
  in 32b.
- **AOrchestratorStation in the production line.** Not registered
  with the Director's DAG yet; just a class definition with the
  Right Things wired.

## Implementation notes (non-contract)

- `EStationType` likely an enum class somewhere in `AssemblyLineTypes.h`.
  Append `Orchestrator` at the end so existing serialized values
  (BP defaults, etc.) don't shift.
- `AgentPromptLibrary` already supports per-agent .md loading via
  `LoadAgentSection`. The new `Orchestrator.md` slots in without
  loader changes.
- `OrchestratorParser` uses the existing `JsonHelpers::ExtractJsonObject`
  + `FJsonSerializer::Deserialize` pattern (same as the Checker's
  verdict parser). Pure-domain — no engine deps beyond
  CoreMinimal + the project's `EStationType` and DAG types.
- The chat subsystem changes for AC32a.3 are minimal: when
  `BuildPromptForStation` is called for Orchestrator, it uses
  `LoadChatSection("OrchestratorChatPromptTemplate")` instead of
  the standard one. One small switch at the prompt-building
  call site.

## What stays unchanged

- All 107 existing specs.
- `AAssemblyLineGameMode::BeginPlay` (hardcoded 4-station boot
  intact).
- `UAssemblyLineDirector`'s DAG behavior (still wired by
  GameMode at boot).
- The 4 production station classes' `ProcessBucket` impls.
- Cinematic camera director.
- Worker FSM.
- All `.md` agent prompts for the existing 4 stations.

## What this unblocks

After 32a, the orchestrator's contract is testable in isolation:
*given a mission text → Claude returns JSON → parser produces
`TArray<FStationNode>`*. Story 32b then plumbs that array into
`SpawnAssemblyLine` and replaces the hardcoded boot.
