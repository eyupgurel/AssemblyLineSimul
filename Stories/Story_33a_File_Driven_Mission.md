# Story 33a — File-driven Orchestrator kickoff

Drafted 2026-05-04. A second entry point to the mission-driven spawn
pipeline introduced in Story 32b: instead of speaking the mission,
read it from a `## Mission` section in `Content/Agents/Orchestrator.md`
and push it through the same chat path. Lets the demo run without a
working microphone (recordings, packaged builds where mic permission
is fiddly, headless walkthroughs).

## Story

> As an operator preparing the demo, I want to write the mission once
> in `Orchestrator.md` and trigger the Orchestrator with a single key
> press (or have it auto-fire at boot) — so the demo is reproducible
> and works without depending on the mic.

## Acceptance Criteria

- **AC33a.1 — `## Mission` section in `Orchestrator.md`.** New
  section under `Content/Agents/Orchestrator.md` containing the
  canonical demo mission in operator-voice (sounds like a transcript,
  not an instruction-to-Claude). Default text: *"Generate ten random
  integers between 1 and 100, filter out only the primes, sort the
  survivors strictly ascending, then check the result against those
  three rules."*

- **AC33a.2 — `AAssemblyLineGameMode::SendDefaultMission()`.** New
  public method. Loads the Mission section via
  `AgentPromptLibrary::LoadAgentSection(Orchestrator, "Mission")`
  and calls `UAgentChatSubsystem::SendMessage(Orchestrator, Mission)`.
  No new spawn code — the existing `OnDAGProposed` →
  `HandleDAGProposed` pipeline takes over. No-op (with Warning log)
  if the Mission section is empty.

- **AC33a.3 — `M` key binding.** Pressing **M** (for "Mission") in
  PIE invokes `SendDefaultMission()`. Wired through the same Enhanced
  Input pattern as the existing Space-for-voice binding. Independent
  of Space — both work in the same session. (Originally bound to
  Enter; PIE intercepts Enter for editor shortcuts so it never reached
  the game viewport — switched to M post-PIE-check.)

- **AC33a.4 — `bAutoMissionAtBoot` flag.** New
  `UPROPERTY(EditAnywhere) bool bAutoMissionAtBoot = false;` on
  `AAssemblyLineGameMode`. When true, `BeginPlay` schedules
  `SendDefaultMission()` ~2 s after spawning the Orchestrator (so
  the GI subsystems are wired and the cinematic isn't fighting an
  in-flight spawn). Off by default — voice demo path is unchanged.

- **AC33a.5 — Voice path unchanged.** AC32b voice-driven kickoff
  still works; the new file path is parallel, not replacing.
  Confirmed by re-running the existing `VoiceSubsystemSpec` and
  `AssemblyLineGameModeSpec` tests untouched.

- **AC33a.6 — Tests.**
  - `AgentPromptLibrarySpec`: "`Orchestrator.md` exposes a Mission
    section and the body is non-empty plain English (no JSON,
    no instructions-to-Claude leakage)".
  - `AssemblyLineGameModeSpec`: "`SendDefaultMission` reads the
    Mission section and pushes it through the chat subsystem as a
    user message addressed to the Orchestrator". Uses a stub Chat
    subsystem (or the real one's history) to verify the message
    arrived.
  - `AssemblyLineGameModeSpec`: "`SendDefaultMission` is a no-op
    (logs Warning, does not call SendMessage) when the Mission
    section is empty".

## Out of scope (deferred to Story 33b)

- **Orchestrator authoring agent `.md` files.** The dynamic
  per-mission `.md` generation lives in 33b.
- **`Saved/Agents/` precedence.** Also 33b — until the Orchestrator
  writes there, no precedence is needed.
- **Re-missioning UI** (operator picks from multiple saved missions).
- **Mission section variants** (multiple missions in `Orchestrator.md`,
  picked by name).

## Implementation notes (non-contract)

- The Enter key uses the same `UInputAction` / `UInputMappingContext`
  scaffolding as the Space binding. Add a sibling `MissionAction`
  property + handler.
- `SendDefaultMission` does NOT touch the spawn pipeline directly.
  It writes through the chat subsystem so the same code path the
  voice transcript uses is exercised — so any future change to the
  Orchestrator's `dag` parsing automatically applies to both
  entry points.
- The auto-fire timer fires once per BeginPlay; if PIE restarts it
  fires again. No re-mission within a session.
- Mission section is read on every `SendDefaultMission` call (no
  caching) so a designer editing `Orchestrator.md` while PIE is
  running and re-pressing Enter sees fresh content immediately.
  (`AgentPromptLibrary` caches the parsed sections per process,
  so this requires either a cache-bust hook or just accepting the
  cached value for the life of the PIE session — accept the cached
  value for v1; the agent .md generation in 33b will introduce a
  cache-bust mechanism we can reuse here.)
