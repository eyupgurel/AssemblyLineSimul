# Story 26 — Terse Checker PASS + push-to-talk silences agents

Drafted 2026-04-28.

## Goal

Two operator-experience improvements:

1. **Terse PASS verdict.** When the Checker accepts a bucket, drop
   the LLM-generated reason — just say "Pass." The visible PASS
   cue (green flash + accepted bucket vanishing) already conveys
   "everything checks out"; reciting a one-sentence justification
   slows the cycle and adds no information. REJECT keeps the
   verbose complaint — the audience needs to hear *what* failed.

2. **Push-to-talk silences agents.** The moment the operator
   presses Space, every in-flight agent TTS line is killed so the
   operator isn't fighting an agent for the audio channel.

## Acceptance Criteria

- **AC26.1** — `ACheckerStation::HandleVerdictReply` (and the
  LLM-unreachable fallback) on `R.bAccepted == true` calls
  `SpeakAloud(TEXT("Pass."))` — no `[PASS]` prefix, no reason.
  REJECT path unchanged: still `SpeakAloud("[REJECT] <reason>")`.
  The LLM-unreachable PASS fallback says `"Pass."` too.

- **AC26.2** — `UAgentChatSubsystem` gains
  `void StopSpeaking()` (UFUNCTION BlueprintCallable). Iterates
  the internal `ActiveSayHandles` array, calls
  `FPlatformProcess::TerminateProc` then `CloseProc` on each,
  resets the array. Mac-only side effect; no-op elsewhere.

- **AC26.3** — `UAgentChatSubsystem::SpeakResponse` is changed
  from `const` to non-const so it can mutate the handle store. It
  prunes dead handles (`!IsProcRunning`) on each call so the
  array stays bounded, and it stores the new handle instead of
  immediately `CloseProc`-ing it (the old code released the
  handle before we could ever kill the process).

- **AC26.4** — `AAssemblyLineGameMode::OnVoiceTalkStarted` calls
  `Chat->StopSpeaking()` right after `BeginRecord` — the
  operator's first syllable cuts off any agent that's mid-line.

- **AC26.5** — Tests:
  - **`StationSpec` Checker PASS test** updated: assert
    `LastSpokenForTesting == "Pass."` exactly (drop the existing
    `[PASS]` prefix + reason-embedded assertions).
  - **`StationSpec` Checker REJECT test** unchanged — still
    expects `[REJECT] <reason>` with the verbose explanation.
  - **`StationSpec` Checker LLM-unreachable test** updated: the
    no-Claude PASS fallback now also speaks `"Pass."` — assert
    that.
  - **New `AgentChatSubsystemSpec` test for `StopSpeaking`**:
    after several `SpeakResponse` calls (Mac-only — guard with
    `#if PLATFORM_MAC`), `StopSpeaking()` empties
    `ActiveSayHandles`. (We can't reliably assert the spawned
    `say` subprocesses got SIGKILL in headless, but the
    bookkeeping is testable and is the load-bearing change.)

- **AC26.6** — All other specs still pass.

## Out of scope

- Visual "agent silenced" affordance — the audio cut is the
  affordance.
- Pause / resume of voices — once silenced, the agent doesn't
  resume from where it was cut off.
- Cross-platform parity: voice silencing is macOS-only because
  `SpeakResponse` is already macOS-only (forks `/usr/bin/say`).
  `StopSpeaking` is a no-op on other platforms.
- Suppressing FUTURE TTS during recording (e.g. don't let the
  Checker speak while the user holds Space) — only kills
  currently-playing voices.
