# Story 29 — Log every Claude prompt + response

Drafted 2026-04-28.

## Goal

Make every Claude API exchange visible in the Output Log so the
operator can see what the agents are saying without setting
breakpoints. Two lines per exchange: outbound prompt before the
HTTP send, inbound response on completion. Both at `Display`
level under `LogClaudeAPI`, so they show in PIE and packaged
builds with the default verbosity.

The arrows make scanning the log fast:
```
LogClaudeAPI: → You are the Generator agent on an assembly line. Apply this rule…
LogClaudeAPI: ← {"id":"msg_…","type":"message","role":"assistant","content":[{"type":"text","text":"{\"result\":[3,17,…]}"}]…}
```

## What's added

In `UClaudeAPISubsystem::SendMessage` ([ClaudeAPISubsystem.cpp:48](../Source/AssemblyLineSimul/ClaudeAPISubsystem.cpp#L48)):

1. **Outbound — log the prompt body** at Display level immediately
   before `Req->ProcessRequest()`. Format: `→ <prompt>`.

2. **Inbound — log the raw response body** at Display level on HTTP
   success (the same `FString Body = Response->GetContentAsString()`
   that today is parsed silently). Format: `← <body>`.

That's it. No new module, no new test infrastructure.

## Acceptance Criteria

- **AC29.1** — `UClaudeAPISubsystem::SendMessage` logs the outgoing
  prompt at `Display` level under `LogClaudeAPI` immediately before
  the HTTP request fires. Format: `→ <prompt body>` (single line; the
  prompt's own `\n` characters are preserved by `UE_LOG`).

- **AC29.2** — The HTTP completion handler logs the raw response
  body at `Display` level on success (status 2xx). Format:
  `← <response body>`.

- **AC29.3** — Existing `Warning`-level logs are unchanged:
  `HTTP request failed.` on transport failure and
  `Claude API error %d: %s` on non-2xx still fire as before.

- **AC29.4** — Manual verification: with the demo running, every
  cycle shows 4 `→ ... ←` pairs (one per station ProcessBucket),
  and every voice / chat command shows one extra `→ ... ←` pair.

- **AC29.5** — All 79 existing specs still pass (the change is
  purely additive log calls; no behavior change).

## Out of scope

- Truncating long bodies (logs print the full content).
- A separate `LogWhisper` enhancement for OpenAI Whisper traffic
  — symmetrical follow-up if you want it.
- An in-game UI panel showing recent traffic.
- Persisting traffic to a file separate from the Output Log.
- A test that asserts the log lines fire (UE log capture in
  specs is fiddly and adds test infrastructure for one log call;
  manual verification per AC29.4 is the contract).

## How to use it

In the editor: open **Window → Output Log**, type
`LogClaudeAPI` into the filter box, hit Play. Every exchange
streams in real time.

For packaged builds: launch with `-log` (or attach with
`Console.app`); the same lines appear in the standard log file.
