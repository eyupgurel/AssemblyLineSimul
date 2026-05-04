# Story 33b — Orchestrator authors per-agent prompt files at mission time

Drafted 2026-05-04. Builds on Story 33a (file-driven kickoff). The
Orchestrator's reply now includes a `prompts` object alongside the
`dag`; the runtime composes a full per-agent `.md` body from
Orchestrator-authored Role + the spec's per-node Rule + the static
ProcessBucketPrompt contract, writes it to `Saved/Agents/<Kind>.md`,
invalidates the prompt cache, then spawns the line. Subsequent
voice-chat with a spawned agent uses the Orchestrator-authored Role.

## Story

> As an operator running the demo, I want each spawned agent's
> personality (its `Role`) to be written by the Orchestrator at
> mission time — so the demo's claim "Claude wrote my workforce" is
> literal: the per-agent `.md` files in `Saved/Agents/` are
> Orchestrator output.

## Acceptance Criteria

- **AC33b.1 — `OrchestratorChatPromptTemplate` extended.** The
  template in `Content/Agents/ChatPrompt.md` asks the Orchestrator
  to emit a `prompts` object alongside `dag`:
  ```json
  {"reply":"...","dag":{"nodes":[...]},
   "prompts":{"Generator":"<role prose>","Filter":"<role prose>",...}}
  ```
  Keys are station-type names matching the JSON `type` field
  (Generator/Filter/Sorter/Checker). One entry per spawned kind.
  Values become the agent's `## Role` section.

- **AC33b.2 — `OrchestratorParser::ParsePlan` (new overload).**
  Returns both the `TArray<FStationNode>` (existing) and a
  `TMap<EStationType, FString>` of role-prose. Existing
  `ParseDAGSpec(text, OutNodes)` delegates to `ParsePlan` with a
  thrown-away map so existing call sites stay valid.
  Missing `prompts` field is non-fatal — empty map returned, parse
  succeeds.

- **AC33b.3 — `AgentChatSubsystem::OnDAGProposed` signature change.**
  Changes from `OneParam<TArray<FStationNode>>` to
  `TwoParams<TArray<FStationNode>, TMap<EStationType, FString>>`.
  The chat subsystem extracts both fields from the Claude reply and
  broadcasts both. Empty map when `prompts` absent.

- **AC33b.4 — Loader precedence: Saved beats Content.**
  `AgentPromptLibrary::LoadFileFromAgentsDir` checks
  `FPaths::ProjectSavedDir() / "Agents"` first, then falls back to
  `FPaths::ProjectContentDir() / "Agents"`. Boot-time loads (before
  any mission) hit the Content fallback unchanged. Mirrors the
  existing `LoadAPIKey` precedence pattern.

- **AC33b.5 — `AgentPromptLibrary::InvalidateCache()`.** New public
  function clears both `CachedAgents` and `CachedChat`. Called by the
  spawn handler after writing new `.md` files but before
  `SpawnLineFromSpec` so freshly-spawned stations load the
  Orchestrator-authored prompts (not stale cached static content).

- **AC33b.6 — `WriteOrchestratorAuthoredPrompts` on the GameMode.**
  New private method invoked from `HandleDAGProposed`. For each entry
  in PromptsByKind:
  1. Read the static `## ProcessBucketPrompt` (and `## DerivedRuleTemplate`
     for Checker) from `Content/Agents/<Kind>.md`.
  2. Compose a complete `.md` body: `# <Kind> agent\n\n## Role\n<prose>\n\n## DefaultRule\n<rule from FStationNode>\n\n## ProcessBucketPrompt\n<static>` (+ DerivedRuleTemplate for Checker).
  3. Write atomically to `Saved/Agents/<Kind>.md` (create dir if
     missing).
  4. Log at Display level with the absolute path.

  Trust the Orchestrator's Role text verbatim. The "contract"
  sections come from static — a botched Role can't break JSON
  parsing.

- **AC33b.7 — Voice chat with a spawned agent uses the new Role.**
  After spawn, `BuildPromptForStation(Filter, ...)` reads
  `Saved/Agents/Filter.md` (via the precedence chain), so the
  resulting prompt embeds the Orchestrator-authored Role. Verifiable
  manually in PIE — say *"Hey Filter, what is your role?"* and the
  reply should reflect the mission-specific prose.

- **AC33b.8 — Tests.**
  - `OrchestratorParserSpec`:
    - "ParsePlan extracts the prompts object alongside dag";
    - "ParsePlan with missing prompts returns empty map and still
      parses dag (non-fatal)";
    - "ParsePlan with prompts object whose key isn't a known station
      type logs Warning and skips that entry".
  - `AgentPromptLibrarySpec`:
    - "Saved/Agents/<Kind>.md takes precedence over Content/Agents/<Kind>.md";
    - "InvalidateCache forces re-read on next LoadAgentSection";
    - "Saved/ fallback is gracefully missing — Content/ is read when
      Saved/ doesn't exist".
  - `AgentChatSubsystemSpec`:
    - The existing `OnDAGProposed` test updated to subscribe with the
      new two-param signature; payload contains the parsed prompts
      map (could be empty).
  - `AssemblyLineGameModeSpec`:
    - "HandleDAGProposed writes Saved/Agents/<Kind>.md for every
      entry in PromptsByKind"; uses `FFileHelper::LoadFileToString`
      to read back and assert content.
    - "HandleDAGProposed invalidates the AgentPromptLibrary cache so
      a subsequent LoadAgentSection picks up the new Role"; depends
      on test ordering or explicit cache state.

- **AC33b.9 — Idempotent across re-PIE within a session.** Saved
  files written by a prior PIE session remain on disk. Next PIE
  start uses Content fallback for the boot-time Orchestrator chat
  (which is what we want — Saved entries from the *prior* mission
  shouldn't leak into the next session's Orchestrator prompt). On
  the next mission, the fresh `prompts` overwrite the prior files.
  No `Saved/Agents/Orchestrator.md` ever written (out of scope).

## Out of scope

- **Authoring `Saved/Agents/Orchestrator.md`** — the Orchestrator
  never rewrites its own mission/role. AC explicitly excludes.
- **A "wipe Saved/Agents/" command** — operator can `rm -rf` manually.
- **Validating Orchestrator-authored Role content** (length, profanity).
  Trust the model.
- **Multi-instance per kind** — same Story 32b v1 constraint.

## Implementation notes (non-contract)

- The `prompts` field is intentionally outside `dag` so the parser
  signature stays clean (the existing `ParseDAGSpec` consumes only
  the `dag` content; `ParsePlan` consumes the whole reply object).
- Cache invalidation runs only after a *new* mission's spawn.
  Boot-time and voice-chat-during-running-line paths don't touch the
  cache.
- File writes use `FFileHelper::SaveStringToFile` with the default
  encoding. Atomic-enough for PIE; production-grade atomic-rename
  isn't needed for `Saved/Agents/`.
- `Saved/Agents/` directory is created lazily on first write via
  `IFileManager::Get().MakeDirectory(..., /*Tree=*/true)`.
