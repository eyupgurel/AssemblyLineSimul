# Story 14 — Voice-Driven Agent Dialogue

Approved 2026-04-26.

## Goal

Make voice push-to-talk the primary input channel for talking with agents. (Story 28 later removed the Tab-toggled chat widget entirely; voice is now the **only** channel.) Talk to agents the way you'd talk to a person on a radio:

> "Hey Filter, do you read me?"
> *(Filter highlights, replies via TTS: "Filter here, ready.")*
> "From now on, only filter the odd numbers."
> *(Filter applies the new rule, replies confirming.)*
> "Hey Sorter, do you read me?"
> *(Active agent switches.)*

## Acceptance Criteria

- **AC14.1 — Push-to-talk capture.** Hold **Space** to record microphone audio while held; release to stop. An on-screen mic indicator lights while recording.

- **AC14.2 — Whisper transcription.** On release, the captured audio is POSTed to OpenAI Whisper (`POST /v1/audio/transcriptions`, `model=whisper-1`). Transcript returns asynchronously; failures are logged and surfaced on screen.

- **AC14.3 — Hailing handshake.** If the transcript matches the pattern `hey <AgentName> ... do you read me` (case-insensitive, fuzzy on the name, allows punctuation/filler in the middle), the matched station becomes the **active speaker**. The agent replies via TTS with the radio-style affirmation **"<Agent> here, reading you loud and clear. Go ahead."** (using a proper-cased friendly name — "Generator", "Filter", "Sorter", "Checker"). Its station glows cyan (point light + talk-panel border).

- **AC14.4 — Sticky context + acknowledged commands.** While an agent is active, every subsequent push-to-talk transcript is sent directly to that agent via `UAgentChatSubsystem::SendMessage(StationType, transcript)` — the user does NOT need to repeat the agent's name. Active agent only changes when a new hailing utterance is detected. Every command-reply spoken back is **prefixed with `"<Agent> here. "`** (skipped if Claude already led with the agent name) so the audience always hears who is speaking and gets a per-command acknowledgement that the agent understood.

- **AC14.4b — Verbose Checker on REJECT.** The Checker's prompt produces a thorough 2–4 sentence complaint (up to 350 chars) on rejection — naming every offending value, explaining why each one breaks the rule, and calling out which prior station was responsible. PASS responses stay terse (≤100 chars).

- **AC14.7 — English-only transcription.** The Whisper multipart request body always includes a `language=en` field so transcription doesn't auto-detect to the speaker's UI locale (Turkish, German, etc.). Testable via the public `UOpenAIAPISubsystem::BuildWhisperMultipartBody` helper.

- **AC14.8 — Audible hail handshake.** The hail affirmation is BOTH written to the agent's talk panel (`SpeakStreaming`) AND spoken aloud through the macOS `say` pipeline (`UAgentChatSubsystem::SpeakResponse`, now public). `SpeakResponse` records its last input into `LastSpokenForTesting` so a spec can assert the audible path was invoked.

- **AC14.5 — Key handling.** OpenAI key loaded by a new `UOpenAIAPISubsystem` from `Saved/OpenAIAPIKey.txt` first, then `Content/Secrets/OpenAIAPIKey.txt` — same dual-path pattern as `UClaudeAPISubsystem`. The Secrets folder is gitignored and stages into packaged builds via `+DirectoriesToAlwaysStageAsNonUFS=(Path="Secrets")` (UAT resolves the path relative to Content/, so `Build/Secrets/` would never have staged — this fix lives at `Content/Secrets/`).

- **AC14.6 — Tests.** Specs cover:
  - `VoiceHailParser`: matches "hey filter do you read me", "Hey, Filter — do you read me?", rejects "hey people", correctly extracts station type for all four agents.
  - `UVoiceSubsystem`: starts with no active agent; hailing transition sets active agent; subsequent transcripts route to chat (mock); a different hailing switches the active agent.

## Out of scope

- Wake-word always-on listening (push-to-talk only).
- On-device speech recognition (using cloud Whisper).
- Custom voice cloning / per-agent voices in TTS (sticking with macOS `say`).
