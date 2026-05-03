# Story 31e — Fluent DAG test builder

Drafted 2026-04-29. Final slice of the DAG executor described in
[Docs/DAG_Architecture.md](../Docs/DAG_Architecture.md). Tightens
test setup so Stories 31a–d's DAG fixtures stop drowning in manual
`TArray<FStationNode>` struct-literal boilerplate.

## A note on scope

The architecture doc bundled three things into Story 31e: **(1)
fluent test builder, (2) watermark GC, (3) `Store` trait + in-memory
impl.** This story implements **only (1)**. (2) and (3) are flagged
as *defer-until-needed* per MODE 1's simplicity rule:

- **Watermark GC** is speculative. The DAG is configured at boot and
  runs for the lifetime of the demo; bucket lifetime is bounded by
  cycle completion + Story 31d's post-merge cleanup. There's no
  observed leak. Sui's GC pattern is documented in the architecture
  doc as a reference; we add the implementation when evidence of
  growth appears (orchestrator missions that run for hours, fan-in
  topologies where a parent never arrives, etc.).

- **`Store` trait + `FInMemoryStore`.** Single-impl abstraction with
  no second backend on the horizon is the literal definition of
  YAGNI. The trait boundary is documented; we extract it the day a
  second backend (debug record-and-playback, cold-start replay)
  becomes a real requirement.

The fluent builder, by contrast, has immediate cost-savings every
time we author a new DAG test. Stories 31a, 31c, and 31d each spell
out 3–4 nodes inline; Story 32 will need many more shapes for the
orchestrator's output. Investing in the builder now pays back
across every subsequent story.

If you'd rather pull GC or `Store` into 31e anyway, push back —
this is the moment to do so.

## Story

> As an engineer (and as a future developer authoring tests for
> orchestrator-output DAGs), I want a fluent builder that
> constructs `TArray<FStationNode>` from concise `.Source().Edge()`
> calls, so that DAG-shaped tests stop drowning in manual
> struct-literal boilerplate and complex topologies (fork, merge,
> fork-merge, fan-in, fan-out) read like the diagram they
> represent.

## Acceptance Criteria

- **AC31e.1 — `FDAGBuilder` exists.** New header-only class
  `FDAGBuilder` under `Source/AssemblyLineSimul/DAG/DAGBuilder.h`
  with three methods returning `FDAGBuilder&` (chainable):
  - `Source(const FNodeRef& Ref, const FString& Rule = {})`
  - `Edge(const FNodeRef& From, const FNodeRef& To, const FString& ToRule = {})`
  - `TArray<FStationNode> Build() const`

- **AC31e.2 — `Source` registers a parent-less node.** Given an
  empty builder, when `Source(R, "rule")` is called and `Build()`
  invoked, then the result is a 1-element array with `Ref==R`,
  `Rule=="rule"`, `Parents.Num()==0`.

- **AC31e.3 — `Edge` adds the edge; `To` is auto-created if
  needed.** Given a builder with `Source(A)` only, when
  `Edge(A, B, "rule_for_B")` is called and `Build()` invoked, then
  the result has both A and B; B's `Parents` contains A and B's
  `Rule == "rule_for_B"`.

- **AC31e.4 — Multiple `Edge` calls accumulate parents on `To`.**
  Given a builder with `Source(A)`, `Source(B)`, when
  `Edge(A, C)` then `Edge(B, C)` are called, then C's `Parents`
  contains both A and B (a fan-in node), insertion order
  preserved.

- **AC31e.5 — Multiple `Edge` calls with the same `From`/different
  `To` express fan-out.** Given `Source(A)`, when
  `Edge(A, B)` and `Edge(A, C)` are called, then both B and C
  exist with `Parents = {A}` each.

- **AC31e.6 — `Build()` order is deterministic.** Given a sequence
  of `Source`/`Edge` calls, when `Build()` is called twice, then
  both outputs are identical (same node order). Order is
  insertion order: a node first appears when it's named in
  `Source` or as the `To` of an `Edge`.

- **AC31e.7 — Builder feeds `FAssemblyLineDAG::BuildFromDAG`
  directly.** A builder-produced array passed into `BuildFromDAG`
  succeeds (verified by an existing positive-path
  `AssemblyLineDAGSpec` test re-expressed via the builder).

- **AC31e.8 — `DAGBuilderSpec` covers** the AC31e.2–6 cases plus
  the empty-builder edge case (`Build()` on no calls returns
  empty array).

- **AC31e.9 — Existing test refactors.** `AssemblyLineDAGSpec` and
  `AssemblyLineDirectorSpec` are refactored to use `FDAGBuilder`
  in the helper that constructs the linear-chain fixture and any
  fan-out / fan-in fixtures. Assertions and test names unchanged
  — pure boilerplate reduction.

- **AC31e.10 — Regression net.** All 99 specs from Story 31d
  continue to pass, plus the new `DAGBuilderSpec` tests.

## Out of scope (per the scope-narrowing decision above)

- Watermark GC implementation. The architecture doc's design stays
  authoritative; implementation deferred until needed.
- `IStore` / `FInMemoryStore` extraction. Same.
- Textual DSL parser (Sui's `test_dag_parser.rs` analog) — the
  fluent builder covers current test needs; a DSL becomes
  worthwhile when 50+ DAG fixtures exist.
- Public Blueprint surface (`FDAGBuilder` is C++-only; per MODE 4
  U17 / U20 we don't expose without a caller).

## Implementation notes (non-contract)

- `Source/AssemblyLineSimul/DAG/DAGBuilder.h` — header-only because
  the methods are 5–10 lines each and the implementation is just
  TMap manipulation.
- Internal storage: `TArray<FNodeRef> InsertionOrder` + `TMap<FNodeRef,
  FStationNode> Nodes`. `Build()` iterates `InsertionOrder`,
  emits `Nodes[Ref]` in order.
- `Source` and `Edge` both call a `GetOrAdd(Ref)` helper that
  creates the entry if not present and appends to `InsertionOrder`.
- `Edge` calls `Nodes[To].Parents.AddUnique(From)` so duplicate
  `Edge(A, B)` calls collapse — one parent-edge regardless of how
  many times declared.

## Refactor scope

Files touched by AC31e.9 only:
- `Tests/AssemblyLineDAGSpec.cpp` — `MakeLinearChain()` helper
  re-expresses via builder. (~10 lines saved.)
- `Tests/AssemblyLineDirectorSpec.cpp` — fan-out (1→2, 1→3),
  fan-in (2→1), cycle re-entry fixtures all use builder.
  (~30 lines saved.)

## What this unblocks

Story 32 (orchestrator) can write fixture DAGs in a couple of lines
each. The orchestrator's spec — *"given a mission text, the
returned DAG matches this expected shape"* — becomes ergonomic
instead of a wall of struct literals.
