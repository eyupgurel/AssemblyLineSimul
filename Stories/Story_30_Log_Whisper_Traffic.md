# Story 30 — Log every Whisper request + transcript

Drafted 2026-04-28.

## Goal

Symmetrical to Story 29 (Claude prompt + response logging), but
for the OpenAI Whisper transcription path. After every push-to-talk
release, the operator should be able to scroll the Output Log and
see exactly what bytes went up and what text came back, so a
mis-routing bug can be triaged instantly: was the transcript wrong,
or was the routing wrong?

## What's added

In `UOpenAIAPISubsystem::TranscribeAudio` ([OpenAIAPISubsystem.cpp:94](../Source/AssemblyLineSimul/OpenAIAPISubsystem.cpp#L94)):

1. **Outbound — log a one-line audio summary** at `Display` level
   under `LogOpenAI` immediately before the HTTP request fires.
   We do NOT log raw audio bytes (Whisper accepts ~25 MB; dumping
   that into the log would be useless and slow). Instead the
   summary names what we sent: byte count, MIME type, filename
   hint, model, language. Format:
   ```
   → Whisper request: <bytes> bytes, MIME=<mime>, file=<filename>, model=<model>, lang=en
   ```

2. **Inbound — log the raw response body** at `Display` level on
   HTTP success, mirroring Story 29's `←` line for Claude. The
   response body is already a small JSON like
   `{"text":"hey filter, do you read me"}`, safe to log in full.
   Format:
   ```
   ← <response body>
   ```

That's it. No new module, no new test infrastructure.

## Acceptance Criteria

- **AC30.1** — `UOpenAIAPISubsystem::TranscribeAudio` logs the
  outbound summary at `Display` level under `LogOpenAI`
  immediately before `Req->ProcessRequest()`. The log line
  includes byte count, MIME type, filename hint, model, and
  language.

- **AC30.2** — The HTTP completion handler logs the raw response
  body at `Display` level on success (status 2xx). Format:
  `← <body>`.

- **AC30.3** — Existing `Warning`-level logs are unchanged:
  `Whisper HTTP request failed.` on transport failure and
  `Whisper API error %d: %s` on non-2xx still fire as before.

- **AC30.4** — Manual verification: with the demo running, every
  push-to-talk release shows a `→ Whisper request: …` line
  followed shortly by a `← {"text":"…"}` line in the Output Log.

- **AC30.5** — All 79 existing specs still pass (the change is
  purely additive log calls; no behavior change).

## Out of scope

- Logging the raw audio bytes — useless content, would dwarf
  every other log line. The summary is the contract.
- Truncating long Whisper responses — they're already small JSON.
- Wiring this into a separate file or in-game UI — same
  reasoning as Story 29.
- A spec-level test for the log calls — UE log capture in specs
  is fiddly; manual verification per AC30.4 is the contract.
