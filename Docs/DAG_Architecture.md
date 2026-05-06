# DAG executor — architecture

Authoritative design for the Story 31 / 32 refactor that turns the
hardcoded 4-station chain into a directed-acyclic-graph (DAG) of
agent stations. Influenced by [Sui's Mysticeti DAG](https://github.com/MystenLabs/sui/tree/main/consensus/core/src)
(`block.rs`, `dag_state.rs`, `block_manager.rs`, `linearizer.rs`),
stripped of every consensus concern (signatures, stake, leader
election, rounds, validators, RocksDB, equivocation, certs).

This doc is the source of truth. Stories 31a–31e implement what's
described here. Story 32 (orchestrator) builds on it.

> **Heads up — Story 38 vocabulary.** This doc was authored when
> the actor flowing through the line was `ABucket`. After Story 38
> the actor is `APayloadCarrier` and the data is the typed
> `UPayload` it composes. Code snippets below have been updated
> to the post-Story-38 type names (`APayloadCarrier*` everywhere
> `ABucket*` used to appear); the field name `InboundBuckets` and
> the method name `ProcessBucket` are deliberately kept verbatim
> because they're load-bearing identifiers in tests and prompt
> files. See README §"Payload + Carrier abstraction (Story 38 deep
> dive)" for the why.

---

## Motivation

Today `UAssemblyLineDirector` hardcodes a chain of four stations
(Generator → Filter → Sorter → Checker) and dispatches carriers by
looking up `EStationType`. This works for one mission shape and
nothing else.

The orchestrator agent (Story 32) needs to spawn arbitrary
topologies at runtime — linear chains, parallel branches, merge
points — based on the operator's mission. The DAG executor is the
substrate that makes that possible.

---

## Decisions locked in (recorded for posterity)

| # | Decision | Rationale |
|---|---|---|
| 1 | **Fan-in is multi-input.** `ProcessBucket` receives `TArray<APayloadCarrier*>` (len==1 for the common single-input case). The agent decides what to do with multiple inputs (compare, diff, vote, concat). | Maximally flexible; concat is the easy case the agent can handle in its prompt. Multi-input enables comparison / quorum agents that the orchestrator might want. |
| 2 | **Fan-out spawns K workers.** Each branch gets its own dedicated worker robot carrying its own carrier clone, processing in parallel. | Visually clear (parallel motion conveys the parallel computation); shared workers would defeat the fan-out's whole point. |
| 3 | **No cycles. Ever.** `BuildFromDAG` runs Kahn's algorithm during construction; a cycle is a hard error, not a runtime issue. | DAG means DAG. An orchestrator that emits a cycle is a bug — surface it at boot, not when the line deadlocks. |
| 4 | **GC by watermark** (Sui's pattern). Single monotonic counter, "anything older than N is logically dead." No refcounting. | Simple. Predictable. Matches Sui's hard-won lesson. |
| 5 | **Persistence: none for v1** but with the boundary in place. `Store` trait + `FInMemoryStore` only. Add a real backend later without touching call sites. | DAG fits in memory; demo doesn't need crash survival. The trait is cheap insurance against later refactor pain. |

---

## Layer 1 — Node identity + payload

Direct adaptation of Sui's `BlockRef` / `Block` / `BlockInfo` triple.

```cpp
// Typed identity. (Kind, Instance) is range-scannable: all "Filter" nodes
// are contiguous in any TMap<FNodeRef, ...> — useful for debugging, derived-
// rule walking, and per-kind queries.
struct FNodeRef
{
    EStationType Kind;
    int32        Instance;   // 0..N within Kind
    // operator< over (Kind, Instance), operator==, GetTypeHash overload.
};

// Immutable after FAssemblyLineDAG::BuildFromDAG. Wrapped in TSharedRef so
// readers hold cheap pointer copies — same pattern as Sui's VerifiedBlock.
struct FStationNode
{
    FNodeRef         Ref;
    FString          Rule;        // EffectiveRule for the station (for non-Checker)
    TArray<FNodeRef> Parents;     // edges-on-child only — Sui's "ancestors"
};

// Mutable per-node side-band, keyed by FNodeRef. Sui's BlockInfo equivalent.
struct FStationNodeState
{
    bool                                  bProcessed = false;
    TArray<TWeakObjectPtr<APayloadCarrier>> InboundBuckets;  // fan-in buffer
    int32                                 GcWatermark = 0;   // "completed at tick N"
    int32                                 ChildrenSpawned = 0;  // diagnostic
};
```

**Why typed `(Kind, Instance)`** instead of an opaque GUID: Sui uses
`(round, author, digest)` precisely because it lets them range-scan
("all blocks at round R"). For us, "all Filter nodes" is a useful
range — debugging, derived-rule walking, type-based agent prompt
loading. Opaque GUIDs throw that away.

**Why edges-on-child only:** Sui materializes back-edges (children)
**lazily** in `BlockInfo.children` only when traversal demands it.
We do the same — see Layer 2.

---

## Layer 2 — DAG structure + traversal

```cpp
class FAssemblyLineDAG
{
public:
    // Cycle detection runs here. Returns false + logs error on cycle.
    bool BuildFromDAG(TArray<FStationNode> Nodes);

    // Forward edges (immutable, stored in node).
    TArray<FNodeRef> GetParents(FNodeRef Node) const;

    // Back edges (lazy — computed from parents on first call, cached).
    TArray<FNodeRef> GetChildren(FNodeRef Node) const;

    // Topological queries.
    TArray<FNodeRef> GetSourceNodes() const;     // no parents
    TArray<FNodeRef> GetTerminalNodes() const;   // no children
    TArray<FNodeRef> GetAncestors(FNodeRef Node) const;     // transitive
    TArray<FNodeRef> GetDescendants(FNodeRef Node) const;   // transitive

private:
    TMap<FNodeRef, TSharedRef<const FStationNode>> Nodes;
    TMap<FNodeRef, FStationNodeState>              States;
    mutable TMap<FNodeRef, TArray<FNodeRef>>       ChildrenCache;  // lazy
};
```

**Cycle detection (Kahn's algorithm)** during `BuildFromDAG`:
compute in-degree for every node, repeatedly remove any node with
in-degree 0, bump down in-degree of its children. If we don't reach
every node, the leftovers form a cycle → log + return false.

**Ancestor walks are iterative** with an "already-visited" early
exit — direct lift from `dag_state.rs`'s `ancestors_at_round`:

```cpp
TArray<FNodeRef> Out;
TSet<FNodeRef> Visited;
TArray<FNodeRef> Queue = { Start };
while (Queue.Num() > 0)
{
    FNodeRef N = Queue.Pop();
    if (Visited.Add(N).IsAlreadyInSet()) continue;
    for (FNodeRef P : Nodes[N]->Parents) Queue.Push(P);
    Out.Add(N);
}
```

**No recursion, anywhere.** Sui burned by stack depth on deep
DAGs — every traversal in their codebase uses an explicit work
queue. We follow the same rule.

---

## Layer 3 — Executor (event-driven, Sui's `BlockManager` pattern)

```cpp
class FDAGExecutor
{
public:
    void OnNodeCompleted(FNodeRef Completed, APayloadCarrier* OutCarrier);

private:
    FAssemblyLineDAG& Graph;

    // Two-map fan-in gate — the load-bearing pattern from
    // block_manager.rs. WaitingFor[child] = parents-not-yet-arrived.
    // WaitedOnBy[parent] = children blocked on it.
    TMap<FNodeRef, TSet<FNodeRef>>     WaitingFor;
    TMap<FNodeRef, TArray<FNodeRef>>   WaitedOnBy;

    void DispatchToNode(FNodeRef Node, TArray<APayloadCarrier*> Inputs);
};
```

When a node finishes processing (its worker has delivered the
output carrier), `OnNodeCompleted` walks each child:

1. Records the parent's carrier in `States[Child].InboundBuckets`.
2. Removes the parent from `WaitingFor[Child]`.
3. If `WaitingFor[Child]` is now empty, fires
   `DispatchToNode(Child, Inputs=InboundBuckets)`.
4. **Iterative — no recursion.** Sui learned the hard way.

For the **fan-out** case (one parent, K children), each child gets
its own clone of `OutCarrier`, produced by
`APayloadCarrier::CloneIntoWorld` which deep-clones the typed
`UPayload` via `Payload->Clone(Outer)` and binds a fresh visualizer.
The K worker robots dispatch in parallel.

For the **fan-in** case (one child, K parents), the child waits for
all K parent carriers to arrive before its `ProcessBucket` fires —
and `ProcessBucket` receives the full `TArray<APayloadCarrier*>` so
the agent can compare/diff/vote/concat per its own logic.

---

## Layer 4 — `ProcessBucket` signature change

This is the most invasive code change in the refactor. All four
station impls + their `.md` prompts must update.

```cpp
// Old (pre-Story-31b):
virtual void ProcessBucket(ABucket* Carrier, FStationProcessComplete OnComplete);

// Story 31b — multi-input signature:
virtual void ProcessBucket(TArray<ABucket*> Inputs, FStationProcessComplete OnComplete);

// Story 38 — payload carrier abstraction (current):
virtual void ProcessBucket(const TArray<APayloadCarrier*>& Inputs,
                            FStationProcessComplete OnComplete);
```

**`Inputs.Num() == 1` is the common case** — single-parent nodes
behave identically to today, the impl just reads `Inputs[0]`. The
multi-input case only fires when the node has fan-in (K parents
configured in the DAG).

After Story 38 the impl also `Cast<UExpectedPayload>(B->Payload)`s
at the top of `ProcessBucket` to access typed data. For the default
`UIntegerArrayPayload` that's `P->Items` (the post-Story-38
equivalent of the pre-Story-38 `Bucket->Contents`).

The `.md` prompt for a station that might receive multi-input adds
language to the effect of: *"You may receive multiple input
carriers. Decide based on the rule whether to merge them, compare
them, or pick one."*

---

## Layer 5 — Director integration

`UAssemblyLineDirector` keeps its event-driven `OnComplete →
dispatch-next` flow but the "next station" lookup goes through
`FDAGExecutor::OnNodeCompleted` instead of the hardcoded
`EStationType` chain. Concretely:

- **`BeginPlay`** spawns one `AStation` and one `AWorkerRobot` per
  `FStationNode` in the DAG. Workers' positions come from
  `FStationNode` index in source order (left-to-right layout of the
  station's instance — fan-out branches splay slightly so they're
  visually distinct).
- **Cinematic camera** regenerates shots from the spawned station
  world positions instead of computing positions from
  `LineOrigin + i * StationSpacing`.
- **Checker derived rule** uses `Graph.GetAncestors(CheckerNode)`
  to walk every upstream node and compose their rules in
  topological order — replaces today's hardcoded "Generator + Filter +
  Sorter rules in that fixed order." For a linear 4-node DAG, output
  is byte-identical to today.

---

## GC by watermark

Sui's `dag_state.rs::calculate_gc_round` model adapts cleanly:

```cpp
class FDAGExecutor
{
    int32 CurrentTick = 0;       // monotonically advancing per cycle
    int32 GcWatermark = 0;       // CurrentTick - GcDepth

    // On each node completion, set State.GcWatermark = CurrentTick.
    // Periodically: any node with State.GcWatermark < (CurrentTick - GcDepth)
    // is eligible for cleanup (its carrier can be destroyed, its inbound
    // buffer cleared).
};
```

**No refcounting, no transitive closure walks** for cleanup. Just a
single integer compare. Matches the lesson from Sui:
"logical-deletion-by-watermark is much simpler than reference
counting and tractable when nodes are partitioned by a monotonic
key."

---

## `Store` trait — abstraction without disk

```cpp
class IStore
{
public:
    virtual ~IStore() = default;
    virtual void               WriteNode(const FStationNode& Node) = 0;
    virtual TSharedPtr<const FStationNode> ReadNode(FNodeRef Ref) const = 0;
    virtual void               WriteState(FNodeRef Ref, const FStationNodeState& State) = 0;
    virtual TArray<FNodeRef>   ScanByKind(EStationType Kind) const = 0;
};

class FInMemoryStore : public IStore { /* TMap-backed */ };
```

The trait sits in `Source/AssemblyLineSimul/DAG/Store.h`.
`FInMemoryStore` is the only impl for v1. We reach for the trait
not because we need disk persistence today but because adding it
later (e.g. for replay / debug-record-and-playback) is much cheaper
when call sites are already trait-based.

---

## Test infrastructure

### Fluent builder

```cpp
FDAGBuilder()
    .Source({Generator, 0}, "Generate 10 ints")
    .Edge({Generator, 0}, {Filter, 0}, "Keep primes")
    .Edge({Filter, 0},    {Sorter, 0}, "Ascending")
    .Edge({Sorter, 0},    {Checker, 0})
    .Build();
```

Used by every `DAGExecutorSpec` test. Reads like the diagram.

### Textual DSL (eventual)

For larger topologies — e.g. testing the orchestrator's outputs:

```
DAG {
  G0 = Generator("Generate 20 odd ints")
  F0 = Filter("Keep primes")     <- G0
  F1 = Filter("Keep > 50")       <- G0       # fan-out
  S0 = Sorter("Ascending")       <- F0, F1   # fan-in
  C0 = Checker()                 <- S0
}
```

Sui's `test_dag_parser.rs` is the model. We build this once we
have enough multi-DAG tests that the fluent builder is verbose.

---

## What we explicitly DO NOT do

Recording the things considered and rejected so the next person
doesn't relitigate them:

- **No cycles.** Validated at `BuildFromDAG`. A cycle is an error.
- **No persistence beyond `FInMemoryStore`.** Trait exists for future use.
- **No back-edges materialized eagerly.** Lazy via `GetChildren`.
- **No recursive traversal anywhere.** Iterative work queues only.
- **No reference counting for cleanup.** Watermark only.
- **No `ProcessBucket` overload.** One signature:
  `TArray<APayloadCarrier*>` (post-Story-38; was `TArray<ABucket*>`).
  Single-parent stations just read `Inputs[0]`.
- **No global executor singleton.** `FDAGExecutor` lives on the
  `UAssemblyLineDirector` like everything else.

---

## Migration plan — story breakdown

This architecture is implemented across Stories 31a–31e:

- **31a — DAG foundation.** Layers 1, 2, partial 4. `FNodeRef`,
  `FStationNode`, `FAssemblyLineDAG` (incl. cycle detection),
  ancestor walks, Checker derived rule walks DAG. `BeginPlay`
  re-expresses the 4-station chain as a 4-node DAG. All 79 specs
  pass + `DAGExecutorSpec`. **No user-visible change.**
- **31b — `ProcessBucket` signature change to `TArray<ABucket*>`.**
  All 4 station impls + `.md` prompts updated. Single-input case
  identical to today; multi-input plumbing exists but isn't
  exercised yet. (Story 38 later swaps `ABucket*` →
  `APayloadCarrier*` in this signature.)
- **31c — Fan-out.** Carrier cloning + K worker spawning per branch.
  `FDAGExecutor::OnNodeCompleted` clones for K children. Tests for
  1→2 and 1→3 branching DAGs. (Story 38 reroutes the deep-copy
  through `Payload->Clone(Outer)` instead of touching the carrier's
  fields directly.)
- **31d — Fan-in.** `FDAGExecutor`'s two-map gate fires only when
  all parents arrive. Multi-input prompts reach the agent. Tests for
  2→1 and 3→1 fan-in.
- **31e — Watermark GC + `Store` trait + fluent test builder.**
  Cleanup pass that locks in the doc's GC and storage decisions
  + cleans up boilerplate in the new tests written across 31a–31d.
- **32 — Orchestrator.** New `EStationType::Orchestrator`,
  `Content/Agents/Orchestrator.md` prompt returning the DAG spec
  JSON, mission-driven `BeginPlay` replacing the static 4-station
  boot.

The textual DAG DSL (Sui-inspired parser) is a Story 33 candidate
once the orchestrator is exercising enough non-linear topologies
that the fluent builder becomes the bottleneck in tests.

---

## Layer 6 — Payload + Carrier abstraction (Story 38, post-DAG)

The Story 31 plan above modeled the moving piece as a single
`ABucket` actor with a `TArray<int32> Contents` field and the
billiard-ball visualization baked into the same class. That worked
for one agent genre (numbers + billiard balls). Story 38 carved
the actor into three composable pieces so future agent kinds
(text agents, image agents, audio agents) can plug in without
touching Director / Worker / Cinematic / Station runtime:

```
APayloadCarrier (actor)
├── PayloadClass     : TSubclassOf<UPayload>           (designer-set)
├── VisualizerClass  : TSubclassOf<UPayloadVisualizer> (designer-set)
├── Payload    : UObject     (auto-instantiated from PayloadClass)
└── Visualizer : SceneComp   (auto-instantiated, attached to RootComponent,
                              auto-binds to Payload, rebuilds on OnChanged)
```

**The DAG layer is unchanged.** Every signature in this doc that
mentions `ABucket*` is read as `APayloadCarrier*` post-Story-38;
the DAG executor never inspects payload contents. Stations
`Cast<UExpectedPayload>(B->Payload)` at `ProcessBucket` entry
when they need typed data.

**Fan-out cloning** (Layer 3) goes through
`APayloadCarrier::CloneIntoWorld`, which spawns the same Class
(preserving Blueprint defaults like `PayloadClass`/`VisualizerClass`),
then deep-clones the typed payload via `Payload->Clone(Outer)`.
The visualizer is fresh per clone — presentation is not part of
the data.

**Naming kept verbatim** for git-blame continuity and to avoid
churn in load-bearing identifiers: method `ProcessBucket`, field
`InboundBuckets`, and the colloquial "carrier" in user-facing prose
all stay. The technical/runtime term is `carrier`.

The full design (3 architectural options weighed, why composition
won) lives in [Stories/Story_38_Payload_Carrier_Abstraction.md](../Stories/Story_38_Payload_Carrier_Abstraction.md).
The deep-dive walkthrough (mermaid diagram, station-side cast
pattern, "how to add a new payload type") lives in the README's
"Payload + Carrier abstraction (Story 38 deep dive)" section.
