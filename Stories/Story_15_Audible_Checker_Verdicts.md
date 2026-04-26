# Story 15 — Audible Checker Verdicts (and any station's "speak out loud")

Approved 2026-04-27.

## Goal

When the Checker rejects a bucket and the feedback light flashes red, it stays
silent — the verbose complaint is written onto the talk panel but never spoken
through the macOS `say` pipeline. The audience sees the red light without
hearing why. Same problem applies to PASS: the talk panel updates silently.

The Checker's `ProcessBucket` calls `SpeakStreaming` directly (panel only),
because the verdict path bypasses `UAgentChatSubsystem::HandleClaudeResponse`
(which is what wires `SpeakResponse` for normal chat replies).

## Acceptance Criteria

- **AC15.1 — `AStation::SpeakAloud(Text)` method.** New public method on the
  base station that updates the talk panel **and** speaks the same text through
  `UAgentChatSubsystem::SpeakResponse`. Works for any station, not just the
  Checker. Falls back gracefully (no crash, just no TTS) when the chat
  subsystem isn't available — for headless tests / non-Mac platforms.

- **AC15.2 — Checker uses `SpeakAloud` for verdicts.** Both the PASS and
  REJECT paths in `ACheckerStation::ProcessBucket` use `SpeakAloud` instead
  of `SpeakStreaming`, so the verbose REJECT complaint is audibly heard by
  the audience.

- **AC15.3 — Test coverage.** New spec asserts that calling `SpeakAloud` on
  a station living in a world that has a `UAgentChatSubsystem` propagates
  the text into `LastSpokenForTesting` (proving TTS was invoked).

## Out of scope

- Migrating every existing `SpeakStreaming` call to `SpeakAloud` — that's a
  follow-up if we want everything spoken (currently chat replies already go
  through `SpeakResponse`; only the Checker's direct verdict path was missing).
