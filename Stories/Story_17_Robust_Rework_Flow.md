# Story 17 — Robust Mid-Flight Rework Flow

Drafted 2026-04-27 (awaiting approval).

## The story for the audience

The whole point of the demo is showing AI agents *handling failure*, not
hiding it. The flow we want to prove on stage:

1. Cycle is running.
2. Operator hails the Filter agent and changes its rule mid-flight
   (e.g. "from now on only keep even numbers").
3. The bucket already in flight finishes its current journey untouched —
   leaves the Filter on the *old* rule, gets sorted, reaches the Checker.
4. The Checker (whose effective rule is now derived from Filter's *new*
   rule) catches the inconsistency, complains audibly with specifics, and
   **sends the bucket back to the Filter for rework**.
5. The Filter re-processes the bucket *using the new rule*. The bucket
   now has the wrong items removed. If filter eliminates everything, the
   crate is empty.
6. **If empty**, the bucket vanishes (a visible "RECYCLE" beat, see AC17.7)
   and the Director kicks off a fresh Generator cycle from scratch.
   Otherwise, the rework continues forward — Sorter, then Checker again.
7. If Checker rejects again, GOTO step 4 (send back to whichever station
   is at fault — Filter for content errors, Sorter for ordering).

This story fixes the bug the operator hit
(`"Filter passed odd primes through unchanged"` and
`"Checker accepted odd primes after the rule said evens"`) — both of which
trace to the chat-driven rule update not actually persisting onto
`Filter.CurrentRule`. It also formalises the rework loop so the audience
sees end-to-end failure recovery.

## Acceptance Criteria

### Rule update reliability

- **AC17.1 — Rule update is verifiable + observable.** When
  `UAgentChatSubsystem` parses a `new_rule` from a Claude reply, it MUST:
  1. Write `Station->CurrentRule = NewRule`.
  2. Call `Station->OnRuleSetByChat()`.
  3. Broadcast a new `OnRuleUpdated(StationType, NewRule)` multicast
     delegate (other systems / tests can react).
  4. Log at `Display` level: `[Filter] CurrentRule updated → "<rule>"`.

- **AC17.2 — Audible + visible "rule updated" confirmation.** When a rule
  changes via chat, the affected station says aloud
  **"Rule updated. From now on I will <new rule>."** (in addition to
  Claude's conversational `reply`). The talk panel shows a brief
  `RULE UPDATED` flash so the audience sees the change even if the TTS
  was missed.

### Rework flow

- **AC17.3 — In-flight bucket is NOT cancelled when a rule changes.** The
  bucket already in flight continues forward using whatever each
  downstream station's `CurrentRule` is at the moment that station
  processes it. (No pre-emption — Story rationale: the failure case is
  exactly what we want to demo.)

- **AC17.4 — Checker rejection routes to the responsible station.** On
  REJECT, `UAssemblyLineDirector` dispatches the bucket to
  `Result.SendBackTo` (Filter for content errors, Sorter for ordering
  errors), unchanged from today — but verified by spec.

- **AC17.5 — Rework re-runs the rework station's `ProcessBucket`** using
  its current `CurrentRule`. This is already the wired behavior; the
  new spec just locks it down so a future regression can't quietly skip
  the call.

- **AC17.6 — After rework, the bucket continues forward** through Sorter
  → Checker (not bypassing). If Checker rejects again, GOTO AC17.4. No
  artificial cap on rework loops — the demo wants to show that the
  agents will keep trying until they get it right (or empty out).

- **AC17.7 — Empty bucket → vanish + fresh Generator cycle.** If a
  station's `ProcessBucket` returns a bucket whose `Contents.Num() == 0`
  (Filter removed everything, etc.), the worker still attempts to
  hand off but the Director observes the empty bucket on placement
  and:
  1. Speaks aloud "Bucket empty after rework — recycling. Starting a
     fresh cycle." through the responsible station.
  2. Plays a brief visual fade-out beat on the bucket actor (200–400ms)
     before destroying it.
  3. Calls `StartCycle()` to spawn a new Generator-driven cycle.
  Cinematic returns to the wide shot during the recycle (same path as
  cycle-completion).

### Diagnostics

- **AC17.8 — Every `ProcessBucket` logs the effective rule it's about
  to apply** at `Display` level, e.g.
  `[Filter] ProcessBucket using rule: "Keep only even numbers" — input: [7, 17, 23, 41]`.
  Makes the operator-bug-from-2026-04-27 instantly diagnosable in the
  editor console.

### Tests

- **AC17.9 — Unit test: `UAgentChatSubsystem` rule-write contract.**
  Synthesise a Claude reply containing `new_rule`, feed into the
  response handler, assert:
  - `Station->CurrentRule` == new rule string.
  - `OnRuleUpdated` broadcast fired exactly once with `(StationType, NewRule)`.
  - `Station->OnRuleSetByChat()` was invoked (verifiable via the existing
    `bUseDerivedRule` flag flipping on Checker).

- **AC17.10 — Unit test: Director rework-loop behavior.**
  - Given a registered Filter / Sorter / Checker (test stubs), simulate
    Checker REJECT with `SendBackTo = Filter`. Assert Director dispatches
    the bucket to Filter.
  - Then simulate the Filter completion with an empty-Contents bucket.
    Assert Director destroys that bucket and calls `StartCycle()` again
    (i.e. the recycle path runs).
  - Then simulate Filter completion with non-empty Contents. Assert the
    bucket is forwarded to Sorter (not recycled).

- **AC17.11 — Unit test: `OnRuleUpdated` broadcasts.** Subscribing to
  `OnRuleUpdated` and triggering a rule change via the chat subsystem
  fires the delegate exactly once. Used by future UI / debug surfaces.

## Out of scope

- Visual/cinematic camera-follow during rework (covered by **Story 16**).
- Persisting `CurrentRule` across PIE sessions.
- Limiting rework loops (Director will keep dispatching as long as
  Checker keeps rejecting — by design, demo-friendly).
