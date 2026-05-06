# AssemblyLineSimul

An Unreal Engine 5.7 demo where AI agents — driven by **Anthropic Claude** —
collaborate on an assembly line built from a **runtime-parsed DAG**. Press
Play and only the **Orchestrator** agent stands at the dock; describe a
mission out loud — anything from a *"generate numbers, filter primes, sort,
check"* 4-stage line to a *"generate 20, take only evens, sort descending,
check, then take only the best 2"* 5-stage line with two Filters — and the
Orchestrator returns a JSON spec that materializes the topology. Stations
spawn, workers spawn, the cinematic camera locks onto whichever carrier is
being processed (zoom-dance: wide → mid → close → hold → out), the
Orchestrator-authored Role for each agent gets written to `Saved/Agents/`,
and the first cycle starts. Change your mind mid-session — give a new
mission and the world tears the old line down atomically and rebuilds.
Every station's `CurrentRule` is plain English you can change on the fly
via voice; the Checker derives its verdict rule by walking the DAG
ancestors at read time, can sit anywhere in the pipeline (terminal or
mid-chain), and the cycle completes at whichever node has no DAG
successors — Filter, Sorter, Checker, anything.

The project is a worked example of:

- **LLM-driven game behavior** — Claude Sonnet for reasoning, Whisper for
  STT, macOS `say` for TTS.
- **A pure-domain DAG executor** for the assembly-line topology
  (Sui-inspired: edges-on-child only, lazy back-edge cache, iterative
  BFS, Kahn's-algorithm cycle check at build time, fan-in
  wait-and-collect gate, fan-out carrier cloning, multi-instance per kind).
- **A plug-and-play payload abstraction** (Story 38) — the actor flowing
  through the line is `APayloadCarrier` with a typed `UPayload`
  (data) + `UPayloadVisualizer` (presentation) chosen per Blueprint.
  The bucket-of-numbers + billiard-ball crate is just one
  (Payload, Visualizer) pair; new agent kinds (text, image, audio)
  ship their own pair without touching Director / Worker / Cinematic.
- **Mission-driven spawn** — an Orchestrator agent emits a JSON DAG
  spec from a spoken or file-driven mission; the runtime parses it,
  validates it, and builds the line at runtime — no hardcoded chain,
  any number of stations of any kind in any topology.
- **Orchestrator-authored agents** — alongside the DAG, the Orchestrator
  authors a `## Role` paragraph for each spawned agent. The runtime
  composes a complete `.md` (Role + Rule + static contract sections)
  and writes it to `Saved/Agents/`. Subsequent voice-chat with each
  agent uses Claude's authored prose.
- **Re-missioning teardown** — give a new mission mid-session and the
  previous line (stations, workers, in-flight carriers, cinematic shots,
  stale `Saved/Agents/` files, Director state, pending timers) is torn
  down atomically before the new line spawns.
- **Subject-tracking cinematic camera** — replaces the old static
  per-station shots with a single follow camera that locks onto the
  active carrier and plays a configurable framing-keyframe sequence
  (wide → mid → close → hold) per Working window. Multi-instance
  correct by construction; topology-agnostic.
- **Strict TDD** — 185 automation specs across 19 spec files plus a
  real-Claude FunctionalTest. RED → GREEN → Refactor for every change.
- **Mid-flight rule changes** propagating through a stateful pipeline
  without breaking the cycle.

## Table of contents

1. [What you see when you press Play](#what-you-see-when-you-press-play)
2. [Quick start](#quick-start)
3. [Architecture](#architecture)
   - [System overview](#system-overview)
   - [Boot flow + mission entry points](#boot-flow--mission-entry-points)
   - [Mission-driven spawn pipeline](#mission-driven-spawn-pipeline)
4. [The DAG executor — deep dive](#the-dag-executor--deep-dive)
   - [Why a DAG](#why-a-dag)
   - [Layer 1 — `FNodeRef` + `FStationNode`](#layer-1--fnoderef--fstationnode)
   - [Layer 2 — `FAssemblyLineDAG`](#layer-2--fassemblylinedag)
   - [Layer 3 — Runtime dispatch (fan-out, fan-in, ancestor walks)](#layer-3--runtime-dispatch-fan-out-fan-in-ancestor-walks)
   - [Layer 4 — Authoring (parser + builder)](#layer-4--authoring-parser--builder)
   - [Worked example: the canonical mission as a DAG](#worked-example-the-canonical-mission-as-a-dag)
   - [Re-missioning teardown sequence](#re-missioning-teardown-sequence)
   - [What we deliberately did NOT do](#what-we-deliberately-did-not-do)
5. [Other flows](#other-flows)
   - [Per-cycle pipeline](#per-cycle-pipeline)
   - [Voice loop](#voice-loop)
   - [Chat / rule-update flow](#chat--rule-update-flow)
   - [Orchestrator-authored prompt pipeline](#orchestrator-authored-prompt-pipeline)
   - [Cinematic camera state machine](#cinematic-camera-state-machine)
   - [Payload + Carrier abstraction (Story 38 deep dive)](#payload--carrier-abstraction-story-38-deep-dive)
6. [User stories](#user-stories)
7. [Testing](#testing)
8. [Project layout](#project-layout)
9. [External services & keys](#external-services--keys)
10. [Packaging a standalone build](#packaging-a-standalone-build)
11. [Known limitations / future work](#known-limitations--future-work)

---

## What you see when you press Play

A single humanoid worker robot — the **Orchestrator** — stands alone on
a metallic industrial floor. No assembly line yet; no Generator, Filter,
Sorter, or Checker. The Orchestrator's job is to listen.

You have **two ways** to give it a mission:

- **Press `M`** — the runtime loads the `## Mission` section from
  [Content/Agents/Orchestrator.md](Content/Agents/Orchestrator.md) (the
  canonical "generate, filter primes, sort, check" mission) and routes
  it through the chat subsystem just as if you had spoken it. Hands-free,
  reproducible, microphone-optional.
- **Hold `Space`** and describe a mission out loud — *"Generate twelve
  random integers between one and a hundred, filter only the even
  numbers, sort them descending, then check."* Whisper transcribes;
  the chat subsystem sends to Claude as the active-default Orchestrator
  agent.

(There's also a `bAutoMissionAtBoot` flag on the GameMode for fully
hands-free recordings — flip it on and the canonical mission fires
~2 s after BeginPlay.)

Within ~5–10 s Claude returns a JSON DAG spec **plus** a per-agent
Role paragraph for each spawned station. The runtime:

1. Parses the spec, validates the topology (Kahn's cycle check).
2. Writes each Role to `Saved/Agents/<Kind>.md` and invalidates the
   prompt cache so freshly-spawned stations pick up the Orchestrator's
   prose.
3. Spawns one `AStation` per node (correct subclass for `Kind`) and
   one `AWorkerRobot` per station along the X axis in DAG order.
4. Regenerates the cinematic camera shots from the freshly spawned
   positions.
5. Spawns the feedback actor (red/green flash on Checker verdict).
6. After a 1.5 s wide-overview hold, dispatches an empty carrier to
   every source node in the DAG (typically just the Generator) to
   start the first cycle.

From here on the cycle plays out per whatever topology the Orchestrator
built. For the canonical 4-station Generator → Filter → Sorter → Checker
mission:

1. **Generator** fills the carrier with a fresh batch of integers per
   its `CurrentRule`. The carrier renders as a glowing-gold wireframe
   crate with billiard-style numbered spheres inside. The camera
   locks onto the carrier and zooms wide → mid → close → hold over
   the Working window — same choreography for every Working state.
2. **Filter** carries the carrier to its dock. Claude returns the kept
   subset; the SELECTED balls glow emissive gold for one second while
   the rejected balls stay with their normal painted-number material —
   the audience sees the contrast — then the rejected balls vanish.
3. **Sorter** reorders the kept items.
4. **Checker** verifies the carrier against its **derived** rule.
   `bUseDerivedRule` defaults to true, so at read time
   `GetEffectiveRule` walks the DAG ancestors of the Checker node and
   composes their current rules into one — *"Generator did X, Filter
   did Y, Sorter did Z — does this fit?"*. Mid-flight rule changes
   upstream automatically reach the Checker without any rebuild.
   - **PASS** → green flash, victory close-up, Checker says **"Pass."**,
     the next cycle spawns.
   - **REJECT** → red flash, the Checker complains aloud naming every
     offending value and the responsible station, the rework worker
     carries the carrier back, and the **camera chases the carrier**
     until it docks at the rework station.

**More elaborate missions just work.** A 5-stage line like *"generate
20, take only evens, sort descending, check, then take only the best 2"*
spawns five stations — including **two Filters** of the same Kind doing
different jobs — and a **mid-chain Checker** that verifies the sorted
list and silently forwards (no green flash mid-chain) to the second
Filter. The cycle completes at the actual terminal — Filter/1 in this
case — with the green flash + auto-loop. The camera follows whichever
carrier is being processed regardless of station instance number.

**Hail any agent** with *"Hey Filter, do you read me?"* — the Filter
worker glows green, Filter speaks an affirmation, the next push-to-talk
routes to Filter as a command (e.g. *"Only filter the odd numbers"*).
Filter acknowledges via TTS and every subsequent carrier flows through
the new rule. Voice is the only chat input; pressing Space silences any
in-flight agent voice so you're never fighting the agents for the
audio channel.

**Change your mind mid-session.** Press M again, or voice a different
mission ("I want twelve numbers, only evens, sorted descending"). The
old line — stations, workers, mid-flight carriers, cinematic shots,
even the per-agent `.md` overrides — disappears atomically; the new
spec materializes in the same place. The Orchestrator + your chat
history with it stay across re-missions.

## Quick start

**Requirements:** macOS, UE 5.7, an Anthropic API key, an OpenAI API
key (for Whisper). Both keys are pay-as-you-go API credits — *not*
the ChatGPT Plus / Claude Max subscriptions, which don't include API
access.

```bash
# 1. Clone
git clone git@github.com:eyupgurel/AssemblyLineSimul.git
cd AssemblyLineSimul

# 2. Drop your API keys (gitignored, auto-staged into packaged builds)
echo 'sk-ant-...' > Content/Secrets/AnthropicAPIKey.txt
echo 'sk-...'     > Content/Secrets/OpenAIAPIKey.txt

# 3. Build
"/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" \
    AssemblyLineSimulEditor Mac Development \
    -Project="$PWD/AssemblyLineSimul.uproject"

# 4. Open in editor
open AssemblyLineSimul.uproject
```

In the editor, hit **Play in Editor** (PIE). First Space-press triggers
a macOS microphone permission prompt — click **Allow**. Click into the
PIE viewport so input reaches the game (the green-bordered window).

## Architecture

### System overview

```mermaid
graph TB
  subgraph Player input
    KB_M[Keyboard: M = default mission]
    KB_S[Keyboard: Space hold-to-talk]
    Mic[macOS microphone]
  end

  subgraph Game Mode
    GM[AAssemblyLineGameMode<br/>Boot: Floor + Voice + Orchestrator<br/>OnDAGProposed → Clear → Spawn → Cinematic → Feedback → Cycles<br/>SendDefaultMission reads Orchestrator.md Mission section]
  end

  subgraph World subsystems
    Dir[UAssemblyLineDirector<br/>FAssemblyLineDAG + dispatch + fan-in gate + recycle<br/>StationByNodeRef + RobotByNodeRef multi-instance maps<br/>ClearLineState resets maps + cancels timers<br/>CompleteCycle handles any registered terminal]
  end

  subgraph Game Instance subsystems
    Chat[UAgentChatSubsystem<br/>Per-agent history,<br/>OnRuleUpdated, OnDAGProposed,<br/>SpeakResponse]
    Voice[UVoiceSubsystem<br/>Default-active = Orchestrator,<br/>push-to-talk routing]
    Claude[UClaudeAPISubsystem<br/>Anthropic Messages, MaxTokens=4096<br/>+ LogClaudeAPI prompt/reply trace]
    OpenAI[UOpenAIAPISubsystem<br/>Whisper transcription<br/>+ LogOpenAI request/transcript trace]
  end

  subgraph Spawned at boot
    Orch[AOrchestratorStation<br/>Chat-only meta agent]
  end

  subgraph Spawned per mission
    Stations["N× AStation subclasses<br/>Generator/Filter/Sorter/Checker<br/>Each carries an FNodeRef (Kind, Instance) so two<br/>Filters of the same Kind don't collide"]
    Workers[N× AWorkerRobot<br/>Phase events broadcast FNodeRef]
    Carriers["APayloadCarrier<br/>typed UPayload + UPayloadVisualizer (Story 38)<br/>defaults: UIntegerArrayPayload + UBilliardBallVisualizer<br/>= the wireframe-crate-with-billiard-balls carrier"]
    Cinematic[ACinematicCameraDirector<br/>One wide-overview shot + permanent FollowCamera<br/>Mode: WideOverview / FollowingBucket / ChasingBucket<br/>Framing-keyframe sequence drives the zoom-dance]
    Feedback[AAssemblyLineFeedback<br/>red/green flash lights]
  end

  subgraph DAG layer
    DAG[FAssemblyLineDAG<br/>Pure-domain, no engine deps<br/>Kahn cycle check + lazy back-edges]
    Parser[OrchestratorParser::ParsePlan<br/>JSON → TArray FStationNode + Prompts]
    SavedAgents[Saved/Agents/<br/>Orchestrator-authored .md overrides]
  end

  subgraph Audio bridge
    MacAudio[UMacAudioCapture<br/>AVFoundation Obj-C++]
    Say["/usr/bin/say (macOS TTS)"]
  end

  subgraph External
    AnthropicAPI[(Anthropic API)]
    OpenAIAPI[(OpenAI Whisper API)]
  end

  KB_M --> GM
  KB_S --> GM
  Mic --> MacAudio
  MacAudio --> OpenAI
  OpenAI -- transcript --> Voice
  Voice -- routes to active --> Chat
  Chat -- prompt --> Claude
  Claude --> AnthropicAPI
  Claude -- reply --> Chat
  Chat -- SpeakResponse --> Say

  Chat -- inner JSON --> Parser
  Parser -- nodes + prompts --> Chat
  Chat -- OnDAGProposed broadcast --> GM
  GM -- writes per-Kind .md --> SavedAgents
  GM -- BuildLineDAG + spawn --> Stations
  GM -- spawn --> Workers
  GM -- spawn --> Cinematic
  GM -- spawn --> Feedback
  Dir -- holds --> DAG
  SavedAgents -- precedence over Content/Agents/ --> Stations

  Stations -- ProcessBucket --> Claude
  Claude -- LLM result --> Stations
  Stations -- SpeakAloud verdict TTS --> Chat

  Dir -- dispatches via DAG --> Workers
  Workers -- carry --> Carriers
  Cinematic -- subscribes --> Dir
  Feedback -- subscribes --> Dir
  Voice -- OnActiveAgentChanged --> GM
  GM -- SetActive green glow --> Workers
  GM -- spawns at boot --> Orch
```

### Boot flow + mission entry points

```mermaid
flowchart TD
  Boot([BeginPlay]) --> Floor[SpawnFloor]
  Floor --> Voice[SetupVoiceInput<br/>bind Space + M]
  Voice --> Orch[SpawnOrchestrator<br/>register with Director]
  Orch --> Subscribe[Subscribe to OnDAGProposed]
  Subscribe --> AutoCheck{bAutoMissionAtBoot?}
  AutoCheck -->|yes| AutoTimer[2s timer →<br/>SendDefaultMission]
  AutoCheck -->|no| Idle([Idle, listening])

  Idle -->|press M| SDM[SendDefaultMission<br/>reads ## Mission from<br/>Orchestrator.md]
  Idle -->|hold Space, speak| VoicePath[Whisper → Voice<br/>routes to active-Orchestrator]
  AutoTimer --> SDM

  SDM --> ChatSend[Chat→SendMessage<br/>Orchestrator + mission text]
  VoicePath --> ChatSend

  ChatSend --> ClaudeCall[Claude API call<br/>using OrchestratorChatPromptTemplate]
  ClaudeCall --> ParsePlan[Chat extracts inner JSON,<br/>ParsePlan → Nodes + Prompts]
  ParsePlan --> Broadcast[OnDAGProposed.Broadcast]
  Broadcast --> Handle[GameMode::HandleDAGProposed]
  Handle --> Pipeline([→ Mission-driven spawn pipeline])
```

### Mission-driven spawn pipeline

How a single Claude reply becomes a running assembly line. This is the
hot path: `OnDAGProposed` → `ClearExistingLine` → write prompts →
spawn stations + workers → spawn cinematic → spawn feedback →
`StartAllSourceCycles`.

```mermaid
sequenceDiagram
  participant Chat as ChatSubsystem
  participant GM as GameMode
  participant Lib as AgentPromptLibrary
  participant FS as Saved/Agents/
  participant Dir as Director
  participant Cin as Cinematic
  participant World as World

  Chat->>GM: OnDAGProposed(Nodes, PromptsByKind)
  Note over GM: Story 34 — atomic teardown first
  GM->>GM: ClearExistingLine
  GM->>World: Destroy old stations/workers/carriers/cinematic/feedback
  GM->>FS: Wipe Saved/Agents/{Generator,Filter,Sorter,Checker}.md
  GM->>Dir: ClearLineState (reset maps + DAG + cancel timers)

  Note over GM: Story 33b — write fresh per-agent .md
  GM->>FS: Write composite .md per spawned Kind<br/>(Role from Orchestrator + Rule from spec +<br/>static ProcessBucketPrompt + Checker DerivedRuleTemplate)
  GM->>Lib: InvalidateCache

  Note over GM: Story 32b — spawn the line
  GM->>Dir: BuildLineDAG (Kahn cycle check)
  alt valid spec
    GM->>World: Spawn N AStation actors<br/>(per-Kind subclass, X-axis layout)
    GM->>World: Spawn N AWorkerRobot actors
    GM->>Cin: SpawnCinematicDirector (shots regen from positions)
    GM->>World: SpawnFeedback (re-bind to Director events)
    GM->>Dir: 1.5s timer → StartAllSourceCycles
    Dir->>Dir: For each source node:<br/>spawn empty carrier, DispatchToStation
  else invalid (cycle / dup-kind / unknown type)
    GM->>GM: SpawnLineFromSpec returns false<br/>nothing spawned, error logged
  end
```

---

## The DAG executor — deep dive

The Story 31 work replaced the hardcoded `Generator → Filter → Sorter
→ Checker` chain with a pure-domain DAG layer. Stories 31a–31e
implemented the architecture; Story 32a–32b made the orchestrator emit
DAG specs at runtime; Story 33a–33b added file-driven kickoff +
Orchestrator-authored agents; Story 34 added atomic re-mission
teardown. The full design (with rejected alternatives + Sui code
references) lives in [Docs/DAG_Architecture.md](Docs/DAG_Architecture.md);
this section is the operator-level "what's in the codebase today" view.

### Why a DAG

The Orchestrator decides what the line looks like. To handle anything
beyond "linear chain of 4 fixed stations," dispatch needs an
authoritative topology — what feeds into what — that the runtime
consults instead of a hardcoded `EStationType` ladder. A DAG is the
minimum primitive: nodes (stations), forward edges (parent → child),
no cycles. The "no cycles" constraint is what makes the dispatch
event loop terminating instead of livelocking.

Inspired by **Sui's Mysticeti / Bullshark consensus**: edges-on-child
only, lazy back-edge cache, iterative BFS for traversals, Kahn's
cycle check at build time, watermark GC (deferred). Stripped of
every consensus concern (signatures, stake, leader election, rounds,
RocksDB, certs).

### The four layers at a glance

```mermaid
graph TB
  subgraph "Layer 1 — Identity"
    NodeRef[FNodeRef Kind, Instance<br/>Range-scannable, hashable]
  end

  subgraph "Layer 2 — Topology"
    DAG[FAssemblyLineDAG<br/>BuildFromDAG validates Kahn<br/>GetParents / GetSuccessors / GetAncestors<br/>GetSourceNodes / GetTerminalNodes]
  end

  subgraph "Layer 3 — Runtime"
    Director[UAssemblyLineDirector::OnRobotDoneAt<br/>Walks GetSuccessors not hardcoded chain<br/>Fan-out: clones carrier K times<br/>Fan-in: queue + wait for all parents]
  end

  subgraph "Layer 4 — Authoring"
    Parser[OrchestratorParser::ParseDAGSpec / ParsePlan<br/>JSON → TArray FStationNode]
    Builder[FDAGBuilder fluent test fixture<br/>.Source.Edge.Build]
  end

  NodeRef --> DAG
  DAG --> Director
  Parser --> DAG
  Builder --> DAG
```

### Layer 1 — `FNodeRef` + `FStationNode`

[`Source/AssemblyLineSimul/DAG/AssemblyLineDAG.h`](Source/AssemblyLineSimul/DAG/AssemblyLineDAG.h):

```cpp
USTRUCT()  // Story 35 — promoted to USTRUCT so it can key UPROPERTY TMaps
struct ASSEMBLYLINESIMUL_API FNodeRef
{
    GENERATED_BODY()

    UPROPERTY() EStationType Kind = EStationType::Generator;
    UPROPERTY() int32        Instance = 0;   // 0..N within Kind

    bool operator==(...) const;    // (Kind, Instance) equality
    bool operator<(...)  const;    // lex order on (Kind, Instance)
};
FORCEINLINE uint32 GetTypeHash(const FNodeRef&);  // for TMap/TSet

struct FStationNode
{
    FNodeRef         Ref;
    FString          Rule;        // EffectiveRule for this station (for non-Checker)
    TArray<FNodeRef> Parents;     // forward edges, immutable
};
```

**Why typed `(Kind, Instance)` and not an opaque GUID:** Sui uses
`(round, author, digest)` precisely because it's range-scannable —
"all blocks at round R" is contiguous in any keyed map. Same payoff
for us: "all Filter nodes" is a useful range for debugging,
derived-rule walking, and per-kind chat routing. An opaque GUID
throws all that away. **Story 35 leans on the `Instance` field hard:
multi-instance specs (two Filters in one mission) get distinct
`{Filter, 0}` and `{Filter, 1}` refs that collide-free in every
runtime map.**

**Why edges-on-child only:** Sui materializes back-edges (`children`)
**lazily** in `BlockInfo` only when traversal demands it. We do the
same — see Layer 2's `GetSuccessors`.

**`AStation::NodeRef` field (Story 35).** `AStation` carries an
`FNodeRef NodeRef` member, set automatically when the Director
registers the station via a per-Kind monotonic counter. Workers
read `AssignedStation->NodeRef` to broadcast phase events with the
correct `(Kind, Instance)` so the camera can distinguish Filter/0
from Filter/1.

### Layer 2 — `FAssemblyLineDAG`

```cpp
class ASSEMBLYLINESIMUL_API FAssemblyLineDAG
{
public:
    bool BuildFromDAG(const TArray<FStationNode>& InNodes);  // Kahn cycle check
    TArray<FNodeRef> GetParents     (const FNodeRef&) const;  // immutable
    TArray<FNodeRef> GetSuccessors  (const FNodeRef&) const;  // lazy cache
    TArray<FNodeRef> GetSourceNodes ()                const;
    TArray<FNodeRef> GetTerminalNodes()               const;
    TArray<FNodeRef> GetAncestors   (const FNodeRef&) const;  // iterative BFS
    const FStationNode* FindNode    (const FNodeRef&) const;
    int32 NumNodes()                                  const;

private:
    TArray<TSharedRef<const FStationNode>> Nodes;     // insertion order
    TMap<FNodeRef, int32>                  RefToIndex;
    mutable bool                           bSuccessorCacheBuilt = false;
    mutable TMap<FNodeRef, TArray<FNodeRef>> SuccessorCache;
};
```

**`BuildFromDAG` runs Kahn's algorithm.** It computes in-degree per
node, repeatedly removes nodes with in-degree 0, decrements the
in-degree of their children, and aborts on a cycle:

```mermaid
flowchart TD
  Start([BuildFromDAG]) --> Compute["For each node N:<br/>in_degree N := Parents.Num"]
  Compute --> Init["Queue := all nodes with in_degree == 0"]
  Init --> Loop{Queue empty?}
  Loop -->|no| Pop["N := Queue.Pop()<br/>Visited++<br/>For each child C:<br/>  in_degree[C]--<br/>  if in_degree[C] == 0:<br/>    Queue.Push(C)"]
  Pop --> Loop
  Loop -->|yes| Check{Visited == NumNodes?}
  Check -->|yes — full drain| Done([return true])
  Check -->|no — leftovers form a cycle| Cycle["log Error: cycle detected<br/>Reset DAG<br/>return false"]
```

The `OrchestratorParser` and `SpawnLineFromSpec` both honor the
`false` return: a cyclic spec produces zero spawned actors and a
clear log explaining why.

**`GetSuccessors` is the lazy back-edge cache.** Forward edges
(`Parents`) live in `FStationNode` immutably. Back edges
(`children`) are computed on first call and memoized:

```cpp
TArray<FNodeRef> FAssemblyLineDAG::GetSuccessors(const FNodeRef& Node) const
{
    EnsureSuccessorCache();
    if (const TArray<FNodeRef>* Found = SuccessorCache.Find(Node))
        return *Found;
    return {};
}

void FAssemblyLineDAG::EnsureSuccessorCache() const
{
    if (bSuccessorCacheBuilt) return;
    SuccessorCache.Empty();
    for (const auto& Node : Nodes)
        for (const FNodeRef& P : Node->Parents)
            SuccessorCache.FindOrAdd(P).Add(Node->Ref);
    bSuccessorCacheBuilt = true;
}
```

Pay-as-you-go: linear topologies that only need parent walks never
build the cache. The runtime dispatcher (Layer 3) hits it once per
node-completion event.

**Ancestor walks are iterative.** Direct lift from Sui's
`dag_state.rs::ancestors_at_round` — no recursion, an explicit work
queue, an "already visited" early exit:

```cpp
TArray<FNodeRef> FAssemblyLineDAG::GetAncestors(const FNodeRef& Node) const
{
    TArray<FNodeRef> Out;
    TSet<FNodeRef>   Visited;
    TArray<FNodeRef> Queue;

    for (const FNodeRef& P : GetParents(Node)) Queue.Push(P);
    while (Queue.Num() > 0)
    {
        const FNodeRef N = Queue.Pop();
        bool bAlready = false;
        Visited.Add(N, &bAlready);
        if (bAlready) continue;
        for (const FNodeRef& P : GetParents(N)) Queue.Push(P);
        Out.Add(N);
    }
    return Out;
}
```

**No recursion anywhere in the DAG layer.** Sui learned the hard way
that recursive traversal blows the stack on deep DAGs; we follow the
same rule even at our (currently small) scale.

### Layer 3 — Runtime dispatch (fan-out, fan-in, ancestor walks, multi-instance)

[`UAssemblyLineDirector::OnRobotDoneAt`](Source/AssemblyLineSimul/AssemblyLineDirector.cpp)
is where the DAG meets gameplay. When a worker finishes carrying a
carrier through its station's `ProcessBucket`, this fires.

For most nodes (single successor, no fan-in at the destination), the
dispatch is just *"look up the next node, hand the carrier to its
worker"* — same cost as the old hardcoded chain.

**`FNodeRef`-keyed dispatch end-to-end (Story 35).** Director maps
are keyed on `FNodeRef`, not `EStationType`:
- `StationByNodeRef : TMap<FNodeRef, AStation*>` — registered station per `(Kind, Instance)`.
- `RobotByNodeRef   : TMap<FNodeRef, AWorkerRobot*>` — one worker per spawned station.
- `OnRobotDoneAt(const FNodeRef& Ref, APayloadCarrier*)` is canonical;
  the old `EStationType`-only signature is a thin shim over `{Kind, 0}`
  for backward-compat.
- `DispatchToStation` and the worker-completion lambda capture `FNodeRef`
  end-to-end so Filter/0 finishing consults Filter/0's successors,
  not Filter/1's.

The backward-compat shims `GetStationOfType(EStationType)` and
`GetRobotForStation(EStationType)` resolve to Instance 0 — voice
hails like *"Hey Filter"* still route to the first Filter (per-instance
voice routing is a deferred future story).

**Fan-out (one parent → K successors).** When `GetSuccessors(Node)`
returns more than one, the carrier gets cloned K times via
`APayloadCarrier::CloneIntoWorld` (Story 38: spawn same Class,
deep-clone the typed `Payload` via `Payload->Clone(Outer)`, fresh
visualizer per clone), one clone dispatched to each branch. The
original is destroyed so the K clones aren't ambiguous-looking
duplicates.

```mermaid
flowchart LR
  Source["Source carrier<br/>Payload.Items = [1,2,3]"] --> Done([OnRobotDoneAt])
  Done --> Lookup["GetSuccessors(Node)<br/>returns A, B, C"]
  Lookup --> Clone["For each successor:<br/>Clone = CloneIntoWorld(World, SpawnLoc)<br/>QueueForFanInOrDispatch or DispatchToStation"]
  Clone --> CloneA["Clone A<br/>Payload.Items = [1,2,3]<br/>→ DispatchToStation"]
  Clone --> CloneB["Clone B<br/>Payload.Items = [1,2,3]<br/>→ DispatchToStation"]
  Clone --> CloneC["Clone C<br/>Payload.Items = [1,2,3]<br/>→ DispatchToStation"]
  Clone --> Destroy["Source.Destroy()"]
```

**Fan-in (one child ← K parents).** When a child has more than one
parent in the DAG, dispatch is queued instead of immediate. Two maps
on the Director hold the gate state:

- `WaitingFor[Child] : TSet<FNodeRef>` — parents not yet arrived for
  the current cycle. Lazily initialized from `GetParents(Child)` on
  first arrival.
- `InboundBuckets[Child] : TArray<TWeakObjectPtr<APayloadCarrier>>` —
  the carriers queued so far; weak-ptr for safety against destruction
  windows. (Field name kept "InboundBuckets" for git-blame continuity;
  payload type is now `APayloadCarrier` per Story 38.)

```mermaid
stateDiagram-v2
  [*] --> Empty: BuildLineDAG
  Empty --> Partial: First parent arrives<br/>(init Waits = GetParents(Child))<br/>(Waits.Remove(Parent))<br/>(InboundBuckets.Add(Carrier))
  Partial --> Partial: Another parent arrives<br/>(Waits.Remove(Parent))<br/>(InboundBuckets.Add(Carrier))
  Partial --> Fired: Last parent arrives<br/>(Waits empty)<br/>FireFanInMerge → Child->ProcessBucket(Inputs)
  Fired --> Empty: WaitingFor.Remove(Child)<br/>InboundBuckets.Remove(Child)<br/>Inputs[1..N-1] destroyed<br/>Inputs[0] continues the chain
```

Once `Waits.IsEmpty()`, `FireFanInMerge` fires the child's
`ProcessBucket(Inputs)`. After completion, `Inputs[0]` continues the
dispatch chain (it survives, carrying the merge result); `Inputs[1..N-1]`
are destroyed. The agent decides what the merge means — concatenate,
diff, vote, pick one — by reading the multi-input array in its
`ProcessBucket`.

The wait-state resets per cycle (the merge clears `WaitingFor[Child]`
and `InboundBuckets[Child]` on completion), so successive cycles
re-fan-in correctly.

**Ancestor walks for the Checker's derived rule.**
[`ACheckerStation::GetEffectiveRule`](Source/AssemblyLineSimul/StationSubclasses.cpp)
calls `Director->GetDAG().GetAncestors(CheckerNodeRef)` and composes
each ancestor's `CurrentRule` into the verdict prompt at read time.
Mid-flight rule changes upstream automatically reach the Checker
without needing a recompile or a re-spawn — the next carrier through
the Checker reads fresh ancestor rules. For the typical linear
4-station mission this resolves identically to the old hardcoded
type-lookup; for fan-in topologies it gets the right multi-source
composition for free.

**Checker mid-chain placement (Story 35).** When the Orchestrator
puts a Checker in the middle of the DAG (the "...check, then take
only the best 2" mission shape), the Director honors both placements:
- **Terminal Checker** (no successors): existing PASS/REJECT semantics
  — PASS broadcasts `OnCycleCompleted` + auto-loops; REJECT routes
  via `SendBackTo`.
- **Mid-chain Checker** (has successors): PASS forwards to the
  successor like any other station (no green flash mid-chain — that
  cue is reserved for actual completion); REJECT still routes via
  `SendBackTo`.

**Any registered terminal completes the cycle (Story 37).** Pre-Story-35
only the Checker could be a DAG terminal, so the runtime special-cased
"Checker has no successors → complete cycle." Story 35 made any node
a possible terminal; Story 37 closes the loop. When `OnRobotDoneAt`
fires for a registered node (`DAG.FindNode(Ref) != nullptr`) with no
successors — Filter/1 in the 5-stage mission, Sorter at a fan-in
merge, anything — it broadcasts `OnCycleCompleted(Bucket)` and
auto-loops via the shared `CompleteCycle()` helper. Unregistered
refs still warn (preserves the misconfiguration signal).

### Layer 4 — Authoring (parser + builder)

The DAG is consumed by Layer 3 but **authored** by two paths:

**Production: `OrchestratorParser::ParsePlan`**
([Source/AssemblyLineSimul/DAG/OrchestratorParser.cpp](Source/AssemblyLineSimul/DAG/OrchestratorParser.cpp))
parses the Orchestrator's reply object — the full `{"reply":..., "dag":{"nodes":[...]}, "prompts":{...}}`
shape — and returns both `TArray<FStationNode>` and a
`TMap<EStationType, FString>` of Orchestrator-authored Role prose.
Two passes:

1. Iterate `dag.nodes`, assign each node an `FNodeRef{Kind, Instance}`
   (Instance = N-th node of that Kind, zero-indexed).
2. Resolve each node's `parents` array of JSON IDs against the
   first-pass `id → FNodeRef` map.

Failure modes: malformed JSON, unknown station type, undeclared
parent ID, missing `dag.nodes` array → returns `false` + Error log
under `LogOrchestrator`. The optional `prompts` field is non-fatal:
absent → empty map; unknown station-type key → Warning + skipped
entry.

**Tests: `FDAGBuilder` fluent fixture**
([Source/AssemblyLineSimul/DAG/DAGBuilder.h](Source/AssemblyLineSimul/DAG/DAGBuilder.h)) —
zero-overhead inline builder that reads in DAG terms instead of array
literals:

```cpp
const FNodeRef Gen{EStationType::Generator, 0};
const FNodeRef Flt{EStationType::Filter,    0};
const FNodeRef Srt{EStationType::Sorter,    0};
const FNodeRef Chk{EStationType::Checker,   0};

const TArray<FStationNode> Spec = FDAGBuilder()
    .Source(Gen)
    .Edge(Gen, Flt)
    .Edge(Flt, Srt)
    .Edge(Srt, Chk)
    .Build();

Director->BuildLineDAG(Spec);
```

`AddUnique`-on-parents prevents accidental duplicate edges. Used
across every fan-out / fan-in spec.

### Worked example: the canonical mission as a DAG

The default mission ("generate ten random integers, filter primes,
sort ascending, then check") becomes a 4-node linear DAG. End-to-end
trace from spoken text to spawned actors:

**1. Operator triggers a mission** — presses M (or speaks). The
mission text reaches Claude wrapped in `OrchestratorChatPromptTemplate`.

**2. Claude returns** (schematic):

```json
{
  "reply": "Spinning up a 4-station line...",
  "dag": {
    "nodes": [
      {"id":"gen", "type":"Generator", "rule":"Generate exactly ten random integers between 1 and 100", "parents":[]},
      {"id":"flt", "type":"Filter",    "rule":"Keep only the prime numbers",                              "parents":["gen"]},
      {"id":"srt", "type":"Sorter",    "rule":"Sort the survivors strictly ascending",                    "parents":["flt"]},
      {"id":"chk", "type":"Checker",   "rule":"Verify against the three rules and report",                "parents":["srt"]}
    ]
  },
  "prompts": {
    "Generator": "You are the source of fresh integer batches...",
    "Filter":    "You sift the wheat from the chaff...",
    "Sorter":    "You impose order on what arrives...",
    "Checker":   "You are the final word on whether a carrier passes..."
  }
}
```

**3. `ParsePlan` produces:**

```cpp
TArray<FStationNode> Nodes = {
    { {Generator, 0}, "Generate exactly ten random integers ...",  {} },
    { {Filter,    0}, "Keep only the prime numbers",                { {Generator, 0} } },
    { {Sorter,    0}, "Sort the survivors strictly ascending",      { {Filter,    0} } },
    { {Checker,   0}, "Verify against the three rules and report",  { {Sorter,    0} } },
};
TMap<EStationType, FString> PromptsByKind = {
    { Generator, "You are the source of fresh integer batches..." },
    { Filter,    "You sift the wheat from the chaff..." },
    ...
};
```

**4. `Director->BuildLineDAG(Nodes)`** runs Kahn's:
- in_degrees: `{G:0, F:1, S:1, C:1}`. Queue starts with `{G}`.
- Pop G, decrement F → in_degree(F) = 0, push F. Queue: `{F}`.
- Pop F, decrement S → 0, push S. Queue: `{S}`.
- Pop S, decrement C → 0, push C. Queue: `{C}`.
- Pop C. Done. Visited == 4 == NumNodes → return true.

**5. Topology after `BuildFromDAG`:**

```
       parents: []         parents: [G:0]      parents: [F:0]      parents: [S:0]
   ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
   │ Generator G:0│───▶│  Filter F:0  │───▶│  Sorter S:0  │───▶│ Checker C:0  │
   │ rule: "Gen…" │    │ rule: "Keep…"│    │ rule: "Sort…"│    │ rule: "Ver…" │
   └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
        SOURCE                                                       TERMINAL
```

GetSourceNodes returns `[G:0]`, GetTerminalNodes returns `[C:0]`,
GetAncestors(C:0) returns `[S:0, F:0, G:0]`.

**6. `WriteOrchestratorAuthoredPrompts`** writes four files under
`Saved/Agents/`. Each composite body is:

```markdown
# Filter agent (orchestrator-authored, Story 33b)

## Role
You sift the wheat from the chaff — every integer the Generator
hands you is examined against the strict definition of primality...

## DefaultRule
Keep only the prime numbers

## ProcessBucketPrompt
You are the Filter agent on an assembly line. Apply this rule...
RULE: {{rule}}
INPUT: [{{input}}]
Respond with ONLY a JSON object on a single line, no markdown:
{"result":[<integers>]}
```

`AgentPromptLibrary::InvalidateCache` runs next so freshly-spawned
stations load the Saved/ override (not the cached Content/ default).

**7. `SpawnLineFromSpec(Nodes)`** — for each node in spec order,
spawns the matching `AStation` subclass at `LineOrigin + i ×
StationSpacing`, sets `CurrentRule = Nodes[i].Rule`, registers with
the Director, spawns one `AWorkerRobot` at the station's
`WorkerStandPoint`, and registers it.

**8. `SpawnCinematicDirector`** walks the spawned stations (via
`Director->GetDAG().GetSourceNodes() + GetAncestors(terminals)`),
emits a single wide-overview shot at the centroid + one closeup per
station, writes them into `Cinematic->Shots`. Calls `BindToAssemblyLine`
+ `Start`.

**9. `SpawnFeedback`** spawns the red/green flash actor, binds to
Director's `OnCycleCompleted` and `OnCycleRejected`.

**10. After 1.5 s, `Director->StartAllSourceCycles`** walks
`DAG.GetSourceNodes()` (just `[G:0]` for this DAG), spawns an empty
carrier at G's input slot, dispatches it. The cycle begins.

### Re-missioning teardown sequence

When a second mission arrives mid-session (`HandleDAGProposed` fires
again), the runtime tears down the previous line atomically before
spawning the new one:

```mermaid
sequenceDiagram
  participant Op as Operator
  participant Chat as ChatSubsystem
  participant GM as GameMode
  participant Dir as Director
  participant World as Spawned Actors
  participant FS as Saved/Agents/

  Note over Op,FS: Mission A is running with carrier mid-flight...

  Op->>Chat: New mission via M or voice
  Chat->>GM: OnDAGProposed(Nodes_B, Prompts_B)
  GM->>GM: ClearExistingLine

  Note over GM,World: Atomic teardown — no half-state visible
  GM->>World: Destroy non-Orchestrator stations
  GM->>World: Destroy all worker robots
  GM->>World: Destroy all carriers (in-flight + idle)
  GM->>World: Destroy cinematic + feedback
  GM->>FS: Wipe Generator.md / Filter.md / Sorter.md / Checker.md

  GM->>Dir: ClearLineState
  Dir->>Dir: StationByType reset (preserve Orchestrator entry)
  Dir->>Dir: RobotByStation, WaitingFor, InboundBuckets reset
  Dir->>Dir: DAG = empty
  Dir->>Dir: ClearAllTimersForObject(this)<br/>(recycle + autoloop cancelled)

  Note over GM,FS: ...then spawn mission B fresh
  GM->>FS: Write mission B's per-Kind .md
  GM->>Dir: BuildLineDAG (new spec)
  GM->>World: Spawn N stations + workers
  GM->>World: SpawnCinematicDirector + SpawnFeedback
  GM->>Dir: 1.5s timer → StartAllSourceCycles
```

**What survives across re-missioning:** the Orchestrator station, the
chat history in `UAgentChatSubsystem`, the floor tiles. Everything
else is fresh.

**What enables the timer cancel:** the recycle and auto-loop timers
in `OnRobotDoneAt` use `FTimerDelegate::CreateWeakLambda(this, ...)`
so `World->GetTimerManager().ClearAllTimersForObject(Director)`
catches them. With the older `CreateLambda` form they'd fire
post-clear and dispatch into a destroyed station.

### What we deliberately did NOT do

Design choices recorded so the next person doesn't relitigate them:

- **No cycles, ever.** Validated at `BuildFromDAG`. A cycle is an
  error, not a runtime concern.
- **No back-edges materialized eagerly.** Lazy via `GetSuccessors`'s
  cache on first call.
- **No recursive traversal anywhere** — every walk is iterative with
  an explicit work queue.
- **No reference counting for cleanup.** When clear, we destroy actors
  and reset maps wholesale; in-flight Claude callbacks bail safely
  via `TWeakObjectPtr`.
- **No `ProcessBucket` overload** — one signature:
  `TArray<APayloadCarrier*>`. Single-parent stations just read
  `Inputs[0]`. (Method name `ProcessBucket` and field name
  `InboundBuckets` are kept verbatim post-Story-38 — they're
  load-bearing identifiers in the test suite and prompt files.)
- **No global executor singleton.** `FAssemblyLineDAG` lives on
  `UAssemblyLineDirector` (a `UWorldSubsystem`).
- **No persistence beyond in-memory.** A `Store` trait was sketched
  in [Docs/DAG_Architecture.md](Docs/DAG_Architecture.md) but
  deliberately deferred — the demo doesn't need crash survival.
- **No watermark GC implementation yet.** The struct field is
  reserved; the real cleanup story is the wholesale teardown in
  Story 34. If a long-running session ever needs it, the watermark
  pattern is documented and ready.
- **No multi-instance per Kind in v1** (e.g. two Filters in one
  mission). The DAG executor itself supports it; `SpawnLineFromSpec`
  rejects it because chat / voice routing currently keys on
  `EStationType`. Lifting requires a `FNodeRef → AWorkerRobot` map
  refactor and chat-routing disambiguation. Deferred.

---

## Other flows

### Per-cycle pipeline

The cycle for the typical *"generate, filter, sort, check"* linear
mission. Fan-out / fan-in topologies follow the same dispatch with
cloning + the wait gate.

```mermaid
flowchart LR
  Start([StartAllSourceCycles]) --> Spawn[Spawn empty carrier<br/>at each source's InputSlot]
  Spawn --> G[Generator robot:<br/>walks → ProcessBucket via Claude<br/>fills carrier per CurrentRule]
  G --> F[Filter robot:<br/>walks → ProcessBucket via Claude<br/>kept balls glow gold for 1 s,<br/>then rejected balls vanish]
  F --> S[Sorter robot:<br/>walks → ProcessBucket via Claude<br/>sorts per CurrentRule]
  S --> C[Checker robot:<br/>walks → ProcessBucket via Claude<br/>verifies vs derived rule from DAG ancestors]
  C -->|PASS| Pass[Green flash<br/>chase camera = victory beat<br/>destroy carrier after delay]
  Pass --> AutoLoop{bAutoLoop?}
  AutoLoop -->|yes| Start
  AutoLoop -->|no| Done([wait])

  C -->|REJECT| Reject[Red flash<br/>chase camera ON<br/>OnCycleRejected]
  Reject --> Dispatch[DispatchToStation:<br/>SendBackTo = Filter or Sorter]
  Dispatch --> Rework[Rework worker walks back,<br/>picks up carrier,<br/>re-runs ProcessBucket with current rule]

  Rework --> Empty{Carrier empty<br/>after rework?}
  Empty -->|yes| Recycle[Speak 'Carrier empty — recycling'<br/>OnCycleRecycled<br/>destroy carrier]
  Recycle --> Start
  Empty -->|no, sent back to Filter| S
  Empty -->|no, sent back to Sorter| C
```

### Voice loop

```mermaid
sequenceDiagram
  participant U as Operator
  participant GM as AssemblyLineGameMode
  participant Cap as UMacAudioCapture
  participant AI as UOpenAIAPISubsystem
  participant V as UVoiceSubsystem
  participant Hail as VoiceHailParser
  participant Chat as UAgentChatSubsystem
  participant CA as UClaudeAPISubsystem
  participant Say as macOS say

  U->>GM: hold Space
  GM->>Cap: BeginRecord M4A AAC
  U->>GM: release Space
  GM->>Cap: EndRecord returns bytes and mime
  GM->>AI: TranscribeAudio multipart language=en
  AI-->>GM: transcript (logged under LogOpenAI)
  GM->>V: HandleTranscript

  alt hey AGENT do you read me
    V->>Hail: TryParseHail
    Hail-->>V: matched StationType, fuzzy match
    V->>V: SetActiveAgent
    V->>GM: OnActiveAgentChanged
    GM->>Chat: SpeakResponse Agent here reading you loud and clear
    Chat->>Say: forks usr/bin/say with tempfile
  else any other transcript
    Note over V: default-active = Orchestrator at boot,<br/>so first transcript without a hail goes to it
    V->>Chat: SendMessage activeAgent transcript
    Chat->>CA: SendMessage with role + rule + history (logged under LogClaudeAPI)
    CA-->>Chat: reply JSON contains reply and either new_rule or dag+prompts
    Chat->>Say: SpeakResponse prefixed reply
    opt new_rule present (non-Orchestrator)
      Chat->>Chat: apply CurrentRule, OnRuleUpdated Broadcast
      Chat->>Say: SpeakResponse Rule updated message
    end
    opt dag+prompts present (Orchestrator only)
      Chat->>Chat: ParsePlan, OnDAGProposed Broadcast
      Note right of Chat: GameMode subscribes →<br/>ClearExistingLine →<br/>WriteOrchestratorAuthoredPrompts →<br/>SpawnLineFromSpec → Cinematic →<br/>SpawnFeedback → StartAllSourceCycles
    end
  end
```

### Chat / rule-update flow

```mermaid
sequenceDiagram
  participant U as Operator
  participant Chat as UAgentChatSubsystem
  participant CA as UClaudeAPISubsystem
  participant St as AStation (e.g. Filter)
  participant Ck as ACheckerStation
  participant DAG as FAssemblyLineDAG

  U->>Chat: SendMessage Filter only filter even numbers
  Chat->>CA: prompt via ChatPromptTemplate (role + rule + carrier + history)
  CA-->>Chat: reply JSON has reply field and new_rule field

  Chat->>St: CurrentRule equals Keep only the even numbers
  Chat->>St: OnRuleSetByChat
  Note right of Ck: Checker bUseDerivedRule defaults true<br/>GetEffectiveRule walks DAG.GetAncestors at read time<br/>so the new Filter rule auto-flows into the Checker's verdict
  Chat->>Chat: OnRuleUpdated Broadcast Filter NewRule
  Chat->>Chat: SpeakResponse Filter here plus reply
  Chat->>Chat: SpeakResponse Rule updated message

  Note over St: Next carrier through Filter ProcessBucket builds prompt with the new EffectiveRule and logs ProcessBucket using rule
```

### Orchestrator-authored prompt pipeline

When the Orchestrator returns a `dag` plus a `prompts` object, the
runtime composes a complete `.md` per spawned Kind by combining the
Orchestrator-authored Role with the static contract sections from
`Content/Agents/`. Files land in `Saved/Agents/` which the loader
prefers over `Content/Agents/` — same precedence pattern as the
API-key loader.

```mermaid
flowchart TD
  Reply["Claude reply (inner JSON)<br/>{reply, dag, prompts}"] --> Parse[OrchestratorParser::ParsePlan]
  Parse --> NodesOut[TArray FStationNode]
  Parse --> PromptsOut[TMap EStationType → Role prose]

  PromptsOut --> Loop[For each Kind in PromptsByKind]
  Loop --> ReadStatic[Read static Content/Agents/Kind.md<br/>extract ProcessBucketPrompt<br/>+ DerivedRuleTemplate if Checker]
  Loop --> NodeRule[Look up matching FStationNode.Rule<br/>from the spec]
  ReadStatic --> Compose[Compose composite body:<br/># Kind agent<br/>## Role  ← Orchestrator<br/>## DefaultRule  ← spec.Rule<br/>## ProcessBucketPrompt  ← static<br/>## DerivedRuleTemplate  ← static if Checker]
  NodeRule --> Compose
  Compose --> Write[Write Saved/Agents/Kind.md]

  Write --> Invalidate[AgentPromptLibrary::InvalidateCache]
  Invalidate --> Spawn[SpawnLineFromSpec → freshly-spawned stations<br/>load via Saved precedence]

  subgraph "Loader precedence (Story 33b)"
    direction LR
    Saved[Saved/Agents/Kind.md] -.->|wins if present| Result
    Content[Content/Agents/Kind.md] -.->|fallback| Result
  end
```

The static contract sections (`ProcessBucketPrompt` with its
`{"result":[…]}` JSON contract; `DerivedRuleTemplate` for the
Checker) are deliberately NOT authored by the Orchestrator — a
botched Role is harmless prose, but a botched parse contract would
break gameplay.

### Cinematic camera architecture (Story 36 deep dive)

Story 36 replaced the static-shot-per-station model with a
**subject-tracking** camera that locks onto whichever carrier is
currently being processed and plays a **framing keyframe sequence**
(wide → mid → close → hold) over the Working window. Multi-instance
correct by construction (no per-station shot table to collide); chase
+ follow share one camera actor; topology-agnostic.

#### Mode state machine

```mermaid
stateDiagram-v2
  [*] --> WideOverview
  WideOverview --> FollowingBucket: HandleStationActive(FNodeRef)<br/>(GetRobotByNodeRef → carrier → EnterFollowingBucket)
  FollowingBucket --> WideOverview: HandleStationIdle(FNodeRef) after LingerSecondsAfterIdle
  FollowingBucket --> FollowingBucket: HandleStationActive(other FNodeRef)<br/>(most-recent-subject tiebreak — replace subject + restart sequence)
  FollowingBucket --> WideOverview: subject destroyed mid-tick (TWeakObjectPtr null)

  WideOverview --> ChasingBucket: HandleCycleRejected(carrier) or HandleCycleResumed(carrier) PASS
  FollowingBucket --> ChasingBucket: same
  ChasingBucket --> FollowingBucket: HandleStationActive (rework station starts Working)
  ChasingBucket --> WideOverview: subject destroyed (carrier recycled / cycle ended)

  note right of FollowingBucket
    Tick(dt):
    - ElapsedInFollowMode += dt
    - resolve active keyframe by elapsed
    - lerp Offset/FOV from prior to active per BlendTime
    - FollowCamera at Subject->GetActorLocation() + Offset
  end note

  note right of ChasingBucket
    Same FollowCamera actor as FollowingBucket;
    fixed chase Offset (-180, 320, 220) instead of
    keyframe interpolation. Tick re-aims at the carrier.
  end note
```

#### Framing keyframe sequence — the zoom dance

A `FFramingSequence` is an array of `FFramingKeyframe { float Time;
FVector Offset; float FOV; float BlendTime; }`. The default sequence
(authored at `SpawnCinematicDirector` time):

```
t=0.0   wide-on-carrier   offset=(-100, 600, 800)  FOV=70  blend=1.0s
t=2.0   mid-on-carrier    offset=( -50, 400, 500)  FOV=55  blend=1.5s
t=4.5   close-on-carrier  offset=(   0, 250, 280)  FOV=42  blend=1.5s
t=7.0   hold close       offset=(   0, 250, 280)  FOV=42  blend=0.5s
```

Each Working window starts elapsed=0. The active keyframe is the
latest one whose `Time <= elapsed`; offset and FOV blend from the
prior keyframe over the active one's `BlendTime`. When the worker
leaves Working (HandleStationIdle), the camera returns to wide
overview.

```
elapsed (s):  0────1────2────3────4────5────6────7────────────end-of-Working
              │         │              │              │
              wide ────►│              │              │
                        mid ──────────►│              │
                                       close ────────►│
                                                      hold ─────────────►
```

For **per-Kind variation** (Filter gets a side angle, Sorter a
top-down, Checker a confrontational frontal), `FramingByKind :
TMap<EStationType, FFramingSequence>` overrides the default per
station Kind. v1 ships with one shared sequence; per-Kind data is
ready to populate when a scene asks for it.

#### Why subject-tracking instead of static shots

Pre-Story-36 the cinematic held a `Shots[]` array — one fixed camera
per station's known X position — and a `StationCloseupShotIndex :
TMap<EStationType, int32>` lookup. With Story 35's multi-instance
support, two Filters in one mission collapsed into one map entry; the
second Filter went uncovered. Static-shot frames also assumed a fixed
station layout that fan-out / fan-in topologies don't honor.

The subject-tracking model has no per-station map. It tracks an
actor (the carrier) with an offset; works regardless of how many
instances of one Kind exist or where they're laid out. The chase
camera (Story 16) was already doing this for rejected carriers;
Story 36 unified closeup + chase onto the same single `FollowCamera`
actor.

#### What this gives you

- **Multi-instance correct.** Filter/0 and Filter/1 both get the same
  zoom-dance on their respective carriers; no per-station shot
  collision.
- **Topology-agnostic.** Fan-out, fan-in, mid-chain Checker, anything
  the Orchestrator can express — the camera follows wherever the
  active carrier is.
- **Re-mission-safe.** Story 34's atomic teardown destroys the
  cinematic; the new spec's spawn re-spawns it with one shot + one
  follow camera. No leftover per-station shots from the prior
  mission.
- **Self-managed elapsed counter** (`ElapsedInFollowMode`) — tests
  drive `D->Tick(dt)` directly and observe deterministic
  interpolation without depending on world tick scheduling.

### Payload + Carrier abstraction (Story 38 deep dive)

Story 38 carved the "thing flowing through the line" into a
plug-and-play three-piece structure so the demo can host more than
the bucket-of-numbers + billiard-balls genre. The Director, the
Worker FSM, the Cinematic camera, the Feedback flashes, and every
Station base hook see only `APayloadCarrier`; what's *inside* the
carrier (numbers? text? image refs? audio buffers?) and *how it
renders* (numbered spheres? scrolling text? a canvas?) are decided
by the carrier's two pluggable component classes.

#### The three pieces

```mermaid
graph LR
  subgraph "APayloadCarrier — the actor"
    Carrier["APayloadCarrier<br/>SceneRoot + PayloadClass + VisualizerClass"]
    Carrier -- owns --> Payload
    Carrier -- attaches --> Visualizer
  end

  subgraph "UPayload — data"
    Payload["UPayload abstract<br/>ItemCount + IsEmpty + ToPromptString + Clone + OnChanged"]
    IntPayload["UIntegerArrayPayload<br/>TArray int32 Items"]
    Payload -. subclass .-> IntPayload
  end

  subgraph "UPayloadVisualizer — presentation"
    Visualizer["UPayloadVisualizer abstract<br/>BindPayload + Rebuild + HighlightItemsAtIndices"]
    Billiard["UBilliardBallVisualizer<br/>12-edge crate + numbered spheres"]
    Visualizer -. subclass .-> Billiard
  end

  Visualizer -- subscribes to --> Payload
  Payload -- OnChanged broadcast --> Visualizer
  Visualizer -- reads typed data via Cast --> Payload
```

**APayloadCarrier** is the Actor — the thing the worker physically
picks up and walks down the line. It owns a `SceneRoot` for
positioning and two designer-set class properties (`PayloadClass`,
`VisualizerClass`). At `OnConstruction` it instantiates one of each
from those classes, attaches the visualizer to RootComponent, and
calls `Visualizer->BindPayload(Payload)` so the visualizer
re-renders whenever the payload mutates.

**UPayload** is the abstract data UObject. Five virtual hooks:
`ItemCount` / `IsEmpty` (used by Director's empty-bucket recycle
path), `ToPromptString` (rendered into Claude prompts via the
carrier's `GetContentsString` pass-through), `Clone(Outer)` (deep
copy, called by `CloneIntoWorld` for fan-out branches), and the
`OnChanged` multicast delegate that visualizers subscribe to.

**UPayloadVisualizer** is the abstract SceneComponent that renders
the payload as scene primitives. Its `BindPayload` subscribes to
`Payload->OnChanged` so any station mutation auto-triggers `Rebuild`.
`HighlightItemsAtIndices` is the Story 25 "Filter selected glow"
hook, now polymorphic — billiard balls glow gold, future scroll
lines could underline, future canvases could green-border.

#### How a station reads the payload

Every concrete `AStation` subclass casts the carrier's payload to
its expected type at `ProcessBucket` entry and either reads or
writes the typed data. From `StationSubclasses.cpp` (Filter):

```cpp
void AFilterStation::ProcessBucket(const TArray<APayloadCarrier*>& Inputs,
                                    FStationProcessComplete OnComplete)
{
    APayloadCarrier* B = Inputs[0];
    UIntegerArrayPayload* P = Cast<UIntegerArrayPayload>(B->Payload);
    if (!P)
    {
        UE_LOG(LogStation, Warning,
            TEXT("[Filter] expected UIntegerArrayPayload; got %s"),
            *GetNameSafe(B->Payload));
        // Pass through gracefully on type mismatch; the demo's prompt
        // contract still wants "accepted" so the cycle doesn't deadlock.
        FStationProcessResult R; R.bAccepted = true;
        OnComplete.ExecuteIfBound(R);
        return;
    }
    // ... LLM round-trip ...
    P->Items = MoveTemp(KeptNumbers);
    P->OnChanged.Broadcast();   // Visualizer rebuilds; rejected balls vanish
    B->HighlightItemsAtIndices(KeptIndices);  // pass-through to visualizer
}
```

The pattern repeats verbatim across Generator / Filter / Sorter /
Checker. Each station is one cast away from polymorphic; agents
that emit text or images would cast to a different `UPayload`
subclass (e.g. `UTextPayload`, `UImageRefPayload`) and the rest of
the runtime never knows.

#### Adding a new (Payload, Visualizer) pair

The whole point of Story 38 is that this is mechanical:

1. Add `UMyPayload : public UPayload` with whatever fields fit
   (override `ItemCount`, `ToPromptString`, `Clone`).
2. Add `UMyVisualizer : public UPayloadVisualizer` that overrides
   `Rebuild` to read `Cast<UMyPayload>(BoundPayload)` and spawn its
   own scene components.
3. Either (a) write a station subclass that casts to `UMyPayload`
   in its own `ProcessBucket`, or (b) author a Blueprint subclass
   of `APayloadCarrier` setting `PayloadClass = UMyPayload` and
   `VisualizerClass = UMyVisualizer`, and point
   `Director->CarrierClass` at it.

Director / Worker FSM / Cinematic camera / Feedback flashes / DAG
runtime: zero lines change. **The abstraction was paid for in
Story 38 so future agent genres don't have to pay it again.**

#### Why composition (vs inheritance / discriminated union)

Three options were on the table:

1. **Inheritance** — `ABucket` becomes abstract; subclass per genre
   (`ANumbersBucket`, `ATextBucket`, ...). Rejected: every new
   genre forces a new Actor class, and combining "bucket-of-numbers
   data" with "scrolling-text visualization" is impossible without
   diamond inheritance.
2. **Discriminated union** — `ABucket` carries an `EPayloadKind`
   enum + a fat struct; stations switch on the enum. Rejected:
   adds-an-enum-value-touches-everything anti-pattern; doesn't
   compose with arbitrary visualizers; all serialization is
   bespoke.
3. **Composition** (chosen) — carrier composes payload + visualizer
   from class properties. Designer-friendly via Blueprint; new
   genres are pure additions (no enum, no switch); payload data
   and presentation evolve independently.

Story 38's design doc records the call so the next person doesn't
re-litigate it.

#### Visual byte-identity with the pre-Story-38 ABucket

The default `(UIntegerArrayPayload, UBilliardBallVisualizer)` pair
is set in `APayloadCarrier`'s constructor and the
`UBilliardBallVisualizer` constructor `FObjectFinder`-loads
`/Game/M_BilliardBall.M_BilliardBall` itself, so a vanilla
`APayloadCarrier::StaticClass()` spawn produces the exact same
12-edge wireframe crate + per-number colored billiard sphere with
canvas-rendered numbered texture as the pre-Story-38 `ABucket`.
**No Blueprint subclass is required** for the typical
4-station demo to look identical.

One real correction was needed: pre-Story-38 the balls' relative
rotation `FRotator(-90, 0, 0)` interacted with `ABucket`'s
non-uniformly-scaled cube `RootComponent` to skew each ball's
local axes by the parent scale. With Story 38's clean
`USceneComponent` SceneRoot the skew is gone, so the ball rotation
flipped to `FRotator(+90, 0, 0)` to bring the painted-number side
back to the top. Same value in name, same visual outcome — the
parent transform changed under the hood.

## User stories

Stories 1–13 were implemented before the formal `Stories/` folder
existed; their full intent lives in commit messages (`git log
--oneline | tail -30`). Stories 14+ each have a markdown spec under
`Stories/`.

### Phase 1 — Skeleton (stories 1–2)
- **Story 1** ([`155e28b`](https://github.com/eyupgurel/AssemblyLineSimul/commit/155e28b)) — Initial scaffold: 4 stations, 4 workers, async ProcessBucket, basic FSM, the Checker calls Claude for QA, headless `FullCycleFunctionalTest` proves an end-to-end cycle reaches accept.
- **Story 2** ([`0d06f33`](https://github.com/eyupgurel/AssemblyLineSimul/commit/0d06f33)) — Worker FSM stranding fix: sync stations were getting an `Idle` overwrite on completion; added a "stay in current state if completion already advanced us" guard plus a visible LLM "thinking" beat for the Checker.

### Phase 2 — Visual basics (stories 3–5)
- **Story 3** ([`1f7c42e`](https://github.com/eyupgurel/AssemblyLineSimul/commit/1f7c42e)) — Workers can adopt a designer-assigned skeletal mesh; per-station tint via dynamic material instances on the body.
- **Story 4** ([`5521db6`](https://github.com/eyupgurel/AssemblyLineSimul/commit/5521db6)) — Composite mech body from 6 engine `BasicShapes` primitives.
- **Story 5** ([`861ede4`](https://github.com/eyupgurel/AssemblyLineSimul/commit/861ede4)) — Per-station UMG `UStationTalkWidget` (deleted in Story 23).

### Phase 3 — Cinematic & feedback (stories 6–9)
- **Story 6** ([`c3b2f15`](https://github.com/eyupgurel/AssemblyLineSimul/commit/c3b2f15)) — `ACinematicCameraDirector` with declarative `Shots[]`, auto-advance, reactive Checker jump.
- **Story 7** ([`20c7e23`](https://github.com/eyupgurel/AssemblyLineSimul/commit/20c7e23)) — Bumped the FullCycle test `TimeLimit` to fit a real Claude round-trip.
- **Story 8** ([`1f75da4`](https://github.com/eyupgurel/AssemblyLineSimul/commit/1f75da4)) — Designers can swap `UStationTalkWidget` for a Blueprint subclass.
- **Story 9** ([`68b1aad`](https://github.com/eyupgurel/AssemblyLineSimul/commit/68b1aad)) — `AAssemblyLineFeedback` flashes transient green/red point lights on Checker accept / reject.

### Phase 4 — Carrier visualisation (stories 10–11)
- **Story 10** ([`abce2ad`](https://github.com/eyupgurel/AssemblyLineSimul/commit/abce2ad)) — Carrier renders contents as numbered spheres inside a 12-edge wireframe crate.
- **Story 11** ([`4fe0d46`](https://github.com/eyupgurel/AssemblyLineSimul/commit/4fe0d46)) — Spheres become billiard-style: per-number color, runtime canvas-rendered numbers.

### Phase 5 — Cinematic polish (story 12)
- **Story 12** ([`b5fb752`](https://github.com/eyupgurel/AssemblyLineSimul/commit/b5fb752) + [`5c5c3a3`](https://github.com/eyupgurel/AssemblyLineSimul/commit/5c5c3a3) + [`ce1796a`](https://github.com/eyupgurel/AssemblyLineSimul/commit/ce1796a)) — Reactive station closeups with wide-shot resume, slowed pacing, workbench mesh on each station.

### Phase 6 — LLM-driven everything (story 13)
- **Story 13** ([`871b43c`](https://github.com/eyupgurel/AssemblyLineSimul/commit/871b43c) + [`37d2fe5`](https://github.com/eyupgurel/AssemblyLineSimul/commit/37d2fe5)) — Every station's `ProcessBucket` becomes async LLM-driven; chat subsystem updates `CurrentRule` per agent.

### Phase 7 — Voice & output channels (stories 14–15)
- **[Story 14](Stories/Story_14_Voice_Driven_Agent_Dialogue.md)** — Push-to-talk → Whisper → hail parser → sticky-context routing. Whisper pinned to `language=en`.
- **[Story 15](Stories/Story_15_Audible_Checker_Verdicts.md)** — `AStation::SpeakAloud` does panel + macOS `say` together; Checker uses it for both PASS and the verbose REJECT complaint.

### Phase 8 — Failure handling (stories 16–17)
- **[Story 16](Stories/Story_16_Camera_Follows_Rejected_Bucket.md)** — Cinematic chase camera. On REJECT the camera follows the carrier back to the rework station; on PASS the camera holds a "victory beat".
- **[Story 17](Stories/Story_17_Robust_Rework_Flow.md)** — Mid-flight rule changes don't cancel the in-flight carrier; empty carrier after rework triggers a visible recycle and a fresh Generator cycle.

### Phase 9 — Worker / scene polish (stories 18–20)
- **[Story 18](Stories/Story_18_Worker_Visual_Polish.md)** — UE5 Manny mannequin (anim swap, 1.5× scale).
- **[Story 19](Stories/Story_19_Active_Agent_Worker_Glow.md)** — Active-speaker green light on the worker (was on the station).
- **[Story 20](Stories/Story_20_Industrial_Floor.md)** — Stylized metallic-floor asset pack tiled 60×60 under the line.

### Phase 10 — Visual cleanup pivot (stories 21–25)
- **Story 21** — *Abandoned.* Fab "Free Fantasy Work Table" prop pivot offset was too fragile; reverted.
- **[Story 22](Stories/Story_22_Cleanup_After_Gold_Bucket.md)** — Cleanup pass after the gold-carrier pivot.
- **[Story 23](Stories/Story_23_Strip_InWorld_Text.md)** — Stripped every in-world text label. TTS audio preserved.
- **[Story 24](Stories/Story_24_Filter_Selected_Glow.md)** — *Superseded by Story 25.*
- **[Story 25](Stories/Story_25_Filter_Selection_Preview.md)** — Filter selection preview: SELECTED balls glow gold for one second while REJECTED balls remain visible — audience sees the contrast.

### Phase 11 — Operator-experience polish (stories 26–28)
- **[Story 26](Stories/Story_26_Terse_Pass_And_Silence_Agents.md)** — Checker PASS is just **"Pass."**; Space silences in-flight agent voice.
- **[Story 27](Stories/Story_27_Externalize_Agent_Prompts.md)** — Every prompt template moves out of `.cpp` literals into editable `.md` under `Content/Agents/`. New `AgentPromptLibrary`.
- **[Story 28](Stories/Story_28_Remove_Tab_Chat_Widget.md)** — Strip the Tab-toggled `UAgentChatWidget`. Voice push-to-talk is now the only input.

### Phase 12 — Observability (stories 29–30)
- **[Story 29](Stories/Story_29_Log_Claude_Traffic.md)** — Every Claude prompt + response logged under `LogClaudeAPI`.
- **[Story 30](Stories/Story_30_Log_Whisper_Traffic.md)** — Mirror for Whisper under `LogOpenAI`. Together: full `mic → transcript → Claude prompt → Claude reply → station behavior` chain reproducible from logs.

### Phase 13 — DAG executor (stories 31a–31e)
- **[Story 31a](Stories/Story_31a_DAG_Foundation.md)** — `FNodeRef`, `FStationNode`, `FAssemblyLineDAG` with Kahn's cycle check; lazy back-edge cache; iterative BFS for `GetAncestors`. Director's `OnRobotDoneAt` consults `GetSuccessors`. Checker derived rule walks ancestors. Linear chain still byte-identical.
- **[Story 31b](Stories/Story_31b_Multi_Input_Signature.md)** — `AStation::ProcessBucket` signature changes to `(const TArray<APayloadCarrier*>& Inputs, …)`. Sets up multi-input fan-in. (Was `ABucket*` pre-Story-38.)
- **[Story 31c](Stories/Story_31c_Fan_Out.md)** — K > 1 successors → clone carrier K times via `APayloadCarrier::CloneIntoWorld` (post-Story-38: deep-clones the typed `Payload` via `Payload->Clone(Outer)`), dispatch each clone, destroy original.
- **[Story 31d](Stories/Story_31d_Fan_In.md)** — K > 1 parents → wait-and-collect gate. `WaitingFor` + `InboundBuckets`. Merge fires when last parent arrives. `Inputs[0]` survives, `Inputs[1..N-1]` destroyed. Wait state resets per cycle.
- **[Story 31e](Stories/Story_31e_DAG_Test_Builder.md)** — `FDAGBuilder` fluent test fixture. Replaces hand-rolled `FStationNode{...}` literals across 6 spec sites.

### Phase 14 — Orchestrator (stories 32a–32b)
- **[Story 32a](Stories/Story_32a_Orchestrator_Agent.md)** — `EStationType::Orchestrator`, `Content/Agents/Orchestrator.md`, `OrchestratorChatPromptTemplate` (asks for `dag` instead of `new_rule`), `AOrchestratorStation`, `OrchestratorParser::ParseDAGSpec`. Pure additive prep work.
- **[Story 32b](Stories/Story_32b_Mission_Driven_Boot.md)** — The headline. `SpawnAssemblyLine` removed; `SpawnOrchestrator` (boot, one chat-only station) + `SpawnLineFromSpec` (mission, N stations + workers + DAG built). `AgentChatSubsystem::OnDAGProposed` fires; `BeginPlay` subscribes and runs spawn → cinematic regen → `StartAllSourceCycles`. Voice default-active = Orchestrator. v1 enforces single-instance-per-kind.

### Phase 15 — File-driven mission (story 33a)
- **[Story 33a](Stories/Story_33a_File_Driven_Mission.md)** — Second entry point to the mission pipeline. `Orchestrator.md` gets a `## Mission` section with the canonical demo text in operator-voice. New `SendDefaultMission()` reads it and routes through chat. Bound to **M** (PIE eats Enter for editor shortcuts). New `bAutoMissionAtBoot` flag fires the mission ~2 s after BeginPlay for hands-free recordings. Voice path unchanged.

### Phase 16 — Orchestrator-authored prompts (story 33b)
- **[Story 33b](Stories/Story_33b_Orchestrator_Authored_Prompts.md)** — `OrchestratorChatPromptTemplate` extended to ask for a sibling `prompts` object (one Role paragraph per spawned station). New `OrchestratorParser::ParsePlan` extracts both `dag` and `prompts`. `OnDAGProposed` signature extended to two params. `WriteOrchestratorAuthoredPrompts` composes Role + spec.Rule + static `ProcessBucketPrompt` (+ Checker `DerivedRuleTemplate`) into a complete `.md` per Kind, written to `Saved/Agents/`. New `AgentPromptLibrary::InvalidateCache`. Loader checks `Saved/Agents/` before `Content/Agents/`. Bumped `MaxTokens` 512 → 4096 (the Orchestrator's full reply was getting truncated mid-JSON).

### Phase 17 — Re-missioning teardown (story 34)
- **[Story 34](Stories/Story_34_Re_Missioning_Teardown.md)** — Atomic teardown when a new DAG arrives. `ClearExistingLine` destroys non-Orchestrator stations + workers + carriers + cinematic + feedback; wipes `Saved/Agents/<Kind>.md`. `ClearLineState` resets Director maps + DAG; cancels timers via `ClearAllTimersForObject(this)` (recycle/auto-loop refactored to `CreateWeakLambda` so they're trackable). `HandleDAGProposed` orchestrates: clear → write prompts → invalidate cache → spawn → cinematic → feedback → start cycles. Orchestrator station + chat history survive.

### Phase 18 — Multi-instance + subject-tracking camera + any-terminal completion (stories 35–37)

Three tightly-coupled stories shipped together because they layer on
the same operator scenario — the 5-stage mission *"generate 20, take
only evens, sort descending, check, then take only the best 2"*
needed all three to actually run end-to-end.

- **[Story 35](Stories/Story_35_Multi_Instance_Per_Kind.md)** — Multi-instance per Kind support. `FNodeRef` promoted to `USTRUCT`. `AStation` carries an `FNodeRef` field set by Director's per-Kind auto-instance counter. Director maps rekey from `EStationType` to `FNodeRef` (`StationByNodeRef`, `RobotByNodeRef`); backward-compat shims (`GetStationOfType`, `GetRobotForStation`) return Instance 0. `OnRobotDoneAt(EStationType)` becomes a shim over canonical `OnRobotDoneAt(FNodeRef)`. Dispatch chain (`DispatchToStation`, worker callback) carries `FNodeRef` end-to-end so Filter/0 finishing consults Filter/0's successors. Checker handles non-terminal placement: PASS forwards to successor, REJECT routes via `SendBackTo`. `SpawnLineFromSpec` drops the Story 32b duplicate-kind rejection.

- **[Story 36](Stories/Story_36_Subject_Tracking_Camera.md)** — Subject-tracking cinematic camera with framing keyframe sequence. Worker phase events (`OnPickedUp`/`OnPlaced`/`OnStartedWorking`/`OnFinishedWorking`) and Director re-broadcasts (`OnStationActive`/`Idle`) carry `FNodeRef` instead of `EStationType`. `ACinematicCameraDirector` rewritten around `ECinematicMode { WideOverview, FollowingBucket, ChasingBucket }`. One permanent `FollowCamera` shared by Following + Chasing. Self-managed elapsed counter so tests are deterministic. Per-station static closeup shots are gone — `SpawnCinematicDirector` authors exactly ONE wide-overview shot + a default `FFramingSequence` (wide → mid → close → hold over ~7 s) that drives a zoom-dance per Working window. Per-Kind override via `FramingByKind`. The "second Filter is invisible to cinematic" Story 35 documented limitation is fixed by construction — the camera follows whichever carrier is active, no per-station map needed.

- **[Story 37](Stories/Story_37_Any_DAG_Terminal_Completes_Cycle.md)** — Any DAG terminal completes the cycle. Pre-Story-35 only Checker could be a terminal; the runtime special-cased "Checker no successors → complete cycle." Multi-instance shapes put non-Checker nodes at the terminal (Filter/1 in the 5-stage mission), and the runtime froze with a warning. New `CompleteCycle` helper extracted from the Checker-terminal path; the no-successors branch now distinguishes registered terminal (`DAG.FindNode(Ref) != nullptr` → `CompleteCycle`) from unregistered Ref (warning preserved as a misconfiguration signal).

### Phase 19 — Plug-and-play payload abstraction (story 38)

- **[Story 38](Stories/Story_38_Payload_Carrier_Abstraction.md)** — The "thing flowing through the line" stops being one hardcoded class (`ABucket` with a `TArray<int32> Contents` field and a wireframe-crate-with-billiard-balls visualization baked in) and becomes three composable pieces: `APayloadCarrier` (the actor) holds a typed `UPayload` (data) and a `UPayloadVisualizer` (presentation), both pluggable per Blueprint via `PayloadClass` + `VisualizerClass` UPROPERTYs. Default pair (`UIntegerArrayPayload` + `UBilliardBallVisualizer`) preserves byte-identical visuals and behavior for the existing 4-station mission — the `M_BilliardBall` master material is loaded by the visualizer's constructor via `FObjectFinder`, so no Blueprint subclass is required. New agent kinds (text agents, image agents, audio agents) ship a new `UPayload` subclass + matching `UPayloadVisualizer` without editing Director / Worker / Cinematic / Station runtime — stations just `Cast<UExpectedPayload>(B->Payload)` at `ProcessBucket` entry. ABucket deleted; `Bucket` kept as the colloquial visual term, `Carrier` as the technical/runtime term. Bulk rename across 9 production files + 7 specs (`Bucket→Carrier`, `BucketClass→CarrierClass`); `BucketSpec` retired; new specs (`IntegerArrayPayloadSpec` + `PayloadCarrierSpec` + `BilliardBallVisualizerSpec`) replace it. 185/185 specs green. Method names `ProcessBucket` and field names `InboundBuckets` are intentionally kept verbatim — they're load-bearing identifiers in the test suite and agent prompt files.

## Testing

The project uses **UE Automation Specs** (BDD-style `Describe` / `It`)
plus one **FunctionalTest** actor for end-to-end coverage.

**Run the full suite headless:**

```bash
"/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor-Cmd" \
    "$PWD/AssemblyLineSimul.uproject" \
    -ExecCmds="Automation RunTests AssemblyLineSimul; Quit" \
    -unattended -nullrhi -log -NoSplash -ABSLOG=/tmp/auto.log
```

Then `grep -c 'Result={Success}' /tmp/auto.log` for a pass count and
`grep -c 'Result={Fail}' /tmp/auto.log` for a fail count.

**Current coverage: 185 specs across 19 spec files plus the
FunctionalTest** (every spec passes against real Anthropic + OpenAI
APIs when keys are configured; specs that don't need network use
synthesised LLM responses fed through public test seams). The +10
delta over 175 is Story 38: `BucketSpec` retired; replaced by
`IntegerArrayPayloadSpec` + `PayloadCarrierSpec` +
`BilliardBallVisualizerSpec`.

| Spec file | What it locks down |
| --- | --- |
| `AgentChatSubsystemSpec` | Per-agent history isolation, prompt construction, `SpeakResponse` test hook, `OnRuleUpdated` broadcast on chat-driven rule change, `StopSpeaking` empties active-say-handle store, **`OnDAGProposed` (Story 32b/33b) — broadcasts on Orchestrator dag-spec replies with parsed nodes + prompts; silent on `dag: null` and on non-Orchestrator agents; works even when Claude wraps JSON in prose / fences (regression test from a Story 33b PIE-check bug)**. |
| `AgentPromptLibrarySpec` | `LoadAgentSection` returns the right `.md` section; `FormatPrompt` resolves `{{name}}`; **Orchestrator `Mission` section non-empty plain-English (Story 33a); `Saved/Agents/` precedence over `Content/Agents/` (Story 33b); `InvalidateCache` forces re-read on next load**. |
| `AssemblyLineDAGSpec` | Story 31a DAG: `BuildFromDAG` rejects cycles via Kahn's algorithm (returns false + leaves DAG empty); `GetParents` / `GetSuccessors` / `GetAncestors` produce deterministic-order results; lazy back-edge cache builds on first `GetSuccessors` call; source/terminal node detection. |
| `AssemblyLineDirectorSpec` | Worker phase events re-broadcast as `OnStationActive` **with FNodeRef payload (Story 36)**; empty-bucket recycle path; **fan-out (Story 31c) clones K times and destroys source**; **fan-in (Story 31d) wait-and-collect gate fires merge once both parents arrive and re-arms per cycle**; **`StartAllSourceCycles` (Story 32b) dispatches one carrier per source node**; **`ClearLineState` (Story 34) — empties StationByNodeRef (preserves Orchestrator), RobotByNodeRef, WaitingFor, InboundBuckets; resets DAG to NumNodes==0; cancels recycle/autoloop timers via the `CreateWeakLambda` refactor**; **multi-instance per Kind (Story 35) — RegisterStation auto-instances via per-Kind counter, two Filters get distinct `{Filter,0}` and `{Filter,1}` registrations, `OnRobotDoneAt(FNodeRef)` consults the right successors, GetStationOfType backward-compat shim returns Instance 0**; **Checker mid-chain handling (Story 35) — terminal vs mid-chain placement; mid-chain PASS forwards silently, REJECT routes via SendBackTo**; **any DAG terminal completes the cycle (Story 37) — registered terminal broadcasts `OnCycleCompleted` via the new `CompleteCycle` helper; unregistered Ref still warns**. |
| `AssemblyLineFeedbackSpec` | Accept/reject light spawning at the carrier location. |
| `AssemblyLineGameModeSpec` | **`SpawnOrchestrator` (Story 32b) spawns exactly one `AOrchestratorStation` and zero workers + registers with the Director**; **`SpawnLineFromSpec` spawns one station + worker per node, applies per-node rules, picks the right subclass per `Kind`, leaves the world untouched on cycles**; **multi-instance per Kind (Story 35) — accepts a 5-node spec with two Filters, spawns 5 stations + 5 workers, each station's `NodeRef` matches its spec node's NodeRef, GetStationOfType returns Instance 0 (backward-compat shim)**; **`SpawnCinematicDirector` (Story 36) authors exactly ONE wide-overview shot regardless of station count + a non-empty `DefaultFollowSequence` zoom dance + spawns the permanent FollowCamera**; **`SendDefaultMission` (Story 33a) reads Mission section + routes through chat; no-op when chat unavailable**; **`WriteOrchestratorAuthoredPrompts` (Story 33b) writes Saved/Agents/<Kind>.md with Role + spec.Rule + static ProcessBucketPrompt + Checker DerivedRuleTemplate preserved**; **`ClearExistingLine` (Story 34) destroys each actor class, preserves Orchestrator + AssemblyLineFloor tiles, no-op on empty world, wipes stale Saved/Agents/**; **`HandleDAGProposed` re-mission tests — second invocation leaves only mission B's actors (the original duplicate-carrier bug); preserves Orchestrator registration; in-flight carrier destroyed; subsequent reads pick up new mission's Saved/Agents/ Role**; propagates `WorkerRobotMeshAsset` / `CarrierClass` (was `BucketClass` pre-Story-38); `SpawnFloor` (Story 20). |
| `IntegerArrayPayloadSpec` | **Story 38** — `UIntegerArrayPayload` data type: `ItemCount`/`IsEmpty`, `ToPromptString` formats as `[1, 2, 3]`, `Clone(Outer)` deep-copies independently, `SetItems` mutates + broadcasts `OnChanged` once. |
| `PayloadCarrierSpec` | **Story 38** — `APayloadCarrier` integration: `OnConstruction` auto-instantiates `Payload` + `Visualizer` from `PayloadClass`/`VisualizerClass` defaults; visualizer auto-binds to payload; `GetContentsString` delegates to `Payload->ToPromptString` (returns `[]` when null); `CloneIntoWorld` (Story 31c, post-38) returns a distinct actor with deep-cloned payload and a fresh visualizer bound to the clone's payload; `HighlightItemsAtIndices` delegates to visualizer. |
| `BilliardBallVisualizerSpec` | **Story 38** — `UBilliardBallVisualizer`: crate built once with 12 cylinder edges; `Rebuild` on payload change re-spawns one numbered sphere per item; `OnVisualizationRevealed` fires on first non-empty rebuild; `HighlightItemsAtIndices` swaps the highlight material on the targeted balls. |
| `CinematicCameraDirectorSpec` | **Story 36 — subject-tracking camera. Default mode `WideOverview` with no follow subject; `Start` spawns the wide-overview shot camera + the permanent FollowCamera; `EnterFollowingBucket` switches mode + sets subject + places camera at first-keyframe offset; most-recent-subject tiebreak replaces subject + restarts sequence; `Tick` positions FollowCamera at subject + active-keyframe offset; interpolates between keyframes over time (60-step manual tick verifies midway-Z); `FramingByKind` per-Kind override applies when present, falls back to `DefaultFollowSequence`; `HandleStationIdle` returns to WideOverview; subject destroyed mid-tick → falls back to WideOverview; chase preserved (HandleCycleRejected enters ChasingBucket mode); chase + follow share the same FollowCamera actor; null-carrier chase falls back to WideOverview**. |
| `DAGBuilderSpec` | Story 31e fluent fixture: `Source` adds a parent-less node, `Edge(from, to)` adds an edge with `AddUnique` parent dedup, `Build()` returns the right `TArray<FStationNode>`. |
| `OpenAIAPISubsystemSpec` | Whisper multipart body shape: `language=en` pinned, `model=whisper-1`, file part with filename + MIME, raw audio bytes embedded verbatim. |
| `OrchestratorParserSpec` | Story 32a: empty / linear / fan-out / fan-in JSON specs parse correctly; malformed JSON, unknown station type, undeclared parent ID return false + Error log. **Story 33b `ParsePlan`: extracts the prompts object alongside dag; missing prompts non-fatal; unknown station-type key in prompts logs Warning and skips; malformed JSON returns false**. **Story 35 multi-instance: a 5-node spec with two Filters parses into FNodeRef{Filter,0} + FNodeRef{Filter,1} (one per spec entry, distinct Instances; edges resolve correctly across multi-instance)**. |
| `StationSpec` | `SpeakAloud` routes through chat subsystem TTS, Checker PASS speaks just "Pass.", REJECT keeps verbose complaint, LLM-unreachable PASS fallback also speaks "Pass." |
| `StationSubclassesSpec` | `AFilterStation::FindKeptIndices` (Story 25): input/kept index mapping with first-occurrence claiming. |
| `VoiceHailParserSpec` | Canonical hail pattern, case insensitivity, alternative confirmations, rejection of non-hails, fuzzy match (Levenshtein ≤ 2) for Whisper letter swaps. |
| `VoiceSubsystemSpec` | **Default-active = Orchestrator at construction (Story 32b)**, hail switches active agent, sticky-context command routing, second hail switches agent. |
| `WorkerRobotSpec` | FSM phase events **with FNodeRef payload (Story 36 — proves Filter/0 vs Filter/1 distinction carries through)**, body-mesh assignment, tint MIDs, sync vs deferred completion. |
| `FullCycleFunctionalTest` | One full Generator → Filter → Sorter → Checker cycle reaches accept. Calls real Claude. |

The TDD discipline is **strict RED → GREEN → Refactor**:
1. Write a story doc under `Stories/Story_NN_…md` (or update an
   existing one).
2. Add failing spec(s) — confirm RED via headless sweep.
3. Implement the minimum code to flip them GREEN.
4. Run the full sweep — must stay all-green.
5. Commit with a message that names the story and lists the new spec count.

## Project layout

```
AssemblyLineSimul/
├── README.md                 ← you are here
├── AssemblyLineSimul.uproject
├── Build/
│   └── Mac/
│       ├── Resources/        ← engine-generated entitlements + plist template
│       └── Scripts/
│           └── fix_voice_in_packaged_app.sh  ← post-stage Info.plist + codesign fix
├── Config/
│   ├── DefaultEngine.ini     ← GlobalDefaultGameMode = BP_AssemblyLineGameMode +
│   │                           ExtraPlistData NSMicrophoneUsageDescription
│   └── DefaultGame.ini       ← +DirectoriesToAlwaysStageAsNonUFS=(Path="Secrets")
├── Docs/
│   ├── Agent_Instructions.md ← thin pointer to Content/Agents/ .md prompts
│   └── DAG_Architecture.md   ← Layer 1-5 design + 5 locked decisions + Sui refs
├── Content/
│   ├── BP_AssemblyLineGameMode.uasset
│   ├── L_AssemblyDemo.umap
│   ├── M_BilliardBall.uasset    ← master material for billiard balls;
│   │                              UBilliardBallVisualizer auto-loads
│   │                              this in its ctor (Story 38, no BP needed)
│   ├── Agents/               ← Story 27: per-agent prompts (loaded by AgentPromptLibrary)
│   │   ├── ChatPrompt.md     ← shared chat templates (default + OrchestratorChatPromptTemplate)
│   │   ├── Generator.md
│   │   ├── Filter.md
│   │   ├── Sorter.md
│   │   ├── Checker.md
│   │   └── Orchestrator.md   ← Story 32a: chat-only meta agent + Story 33a Mission section
│   ├── Metallic_Floor/       ← Stylized Metallic Floor asset pack (Story 20)
│   └── Secrets/              ← gitignored API keys; auto-staged into packaged builds
│       ├── AnthropicAPIKey.txt
│       └── OpenAIAPIKey.txt
├── Saved/
│   └── Agents/               ← Story 33b: Orchestrator-authored .md overrides
│                                (loader prefers Saved/ over Content/; wiped on re-mission)
├── Source/AssemblyLineSimul/
│   ├── AssemblyLineGameMode.{h,cpp}    ← BeginPlay: Floor + Voice + Orchestrator
│   │                                     OnDAGProposed handler: ClearExistingLine →
│   │                                     WriteOrchestratorAuthoredPrompts → SpawnLineFromSpec →
│   │                                     SpawnCinematicDirector → SpawnFeedback → cycles
│   │                                     SendDefaultMission (M key, Story 33a)
│   ├── AssemblyLineDirector.{h,cpp}    ← Holds FAssemblyLineDAG; StationByNodeRef +
│   │                                     RobotByNodeRef multi-instance maps (Story 35);
│   │                                     OnRobotDoneAt(FNodeRef, APayloadCarrier*) canonical
│   │                                     (Story 38 rename from ABucket*);
│   │                                     fan-in wait gate; recycle; ClearLineState (Story 34)
│   │                                     + WeakLambda timers; CompleteCycle (Story 37) for
│   │                                     any registered terminal
│   ├── AssemblyLineTypes.h             ← EStationType (incl. Orchestrator), FStationProcessResult,
│   │                                     FAgentChatMessage
│   │
│   ├── Station.{h,cpp}                 ← base station: ActiveLight, SpeakAloud (TTS-only),
│   │                                     ProcessBucket(TArray<APayloadCarrier*>, OnComplete)
│   │                                     (Story 38 rename from ABucket*); subclasses
│   │                                     Cast<UIntegerArrayPayload>(B->Payload) at entry;
│   │                                     FNodeRef NodeRef field auto-set by Director (Story 35)
│   ├── StationSubclasses.{h,cpp}       ← Generator, Filter, Sorter, Checker, Orchestrator
│   │                                     Filter::FindKeptIndices for selection preview
│   │                                     Checker::GetEffectiveRule walks DAG ancestors
│   ├── WorkerRobot.{h,cpp}             ← FSM, UE5 Manny mannequin, green ActiveLight;
│   │                                     phase events broadcast FNodeRef (Story 36)
│   │
│   │ ─── Story 38 — Payload + Carrier abstraction (replaces ABucket) ───
│   ├── Payload.{h,cpp}                  ← UPayload (abstract data) + UIntegerArrayPayload
│   │                                     concrete: TArray<int32> Items; ToPromptString;
│   │                                     Clone(Outer) for fan-out deep-copy; OnChanged
│   │                                     multicast that visualizers subscribe to
│   ├── PayloadVisualizer.{h,cpp}        ← UPayloadVisualizer (abstract SceneComponent) +
│   │                                     UBilliardBallVisualizer concrete: 12-edge wireframe
│   │                                     crate + per-item canvas-rendered numbered spheres,
│   │                                     HighlightItemsAtIndices for Filter selection,
│   │                                     auto-rebuilds on Payload->OnChanged
│   ├── PayloadCarrier.{h,cpp}           ← APayloadCarrier actor: PayloadClass +
│   │                                     VisualizerClass UPROPERTYs, OnConstruction
│   │                                     instantiates both, attaches Visualizer to RootComponent
│   │                                     and binds Payload; CloneIntoWorld for fan-out
│   │
│   ├── DAG/                            ← Story 31 — pure-domain DAG layer
│   │   ├── AssemblyLineDAG.{h,cpp}     ← FNodeRef, FStationNode, FAssemblyLineDAG
│   │   ├── DAGBuilder.h                ← Story 31e fluent test fixture
│   │   └── OrchestratorParser.{h,cpp}  ← Story 32a/33b JSON spec parser (ParseDAGSpec + ParsePlan)
│   │
│   ├── ClaudeAPISubsystem.{h,cpp}      ← Anthropic /v1/messages POST + LogClaudeAPI trace
│   │                                     MaxTokens=4096 (Story 33b)
│   ├── OpenAIAPISubsystem.{h,cpp}      ← Whisper /v1/audio/transcriptions + LogOpenAI trace
│   ├── AgentChatSubsystem.{h,cpp}      ← per-agent chat, OnRuleUpdated, OnDAGProposed (TwoParams),
│   │                                     SpeakResponse (TTS), StopSpeaking
│   ├── AgentPromptLibrary.{h,cpp}      ← Story 27 — loads .md prompts; Story 33b — Saved/Agents/
│   │                                     precedence + InvalidateCache
│   ├── VoiceSubsystem.{h,cpp}          ← active-agent state (default = Orchestrator), routing
│   ├── VoiceHailParser.{h,cpp}         ← "hey <agent> do you read me" matcher (Levenshtein)
│   ├── MacAudioCapture.{h,mm}          ← AVAudioRecorder Obj-C++ bridge (Mac-only)
│   │
│   ├── CinematicCameraDirector.{h,cpp} ← Story 36 — subject-tracking camera with
│   │                                     ECinematicMode {WideOverview, FollowingBucket,
│   │                                     ChasingBucket}; one wide overview shot + one permanent
│   │                                     FollowCamera; FFramingKeyframe sequence drives the
│   │                                     wide → mid → close → hold zoom dance per Working
│   │                                     window; FramingByKind for per-Kind overrides
│   ├── AssemblyLineFeedback.{h,cpp}    ← red/green flash lights on Checker verdict
│   ├── JsonHelpers.h                   ← shared ExtractJsonObject for chatty LLM replies
│   │
│   └── Tests/                          ← all *Spec.cpp + the FunctionalTest actor
└── Stories/                            ← markdown specs for stories 14-38 (21 abandoned)
```

## External services & keys

| Service | Endpoint | Purpose | Where the key lives |
| --- | --- | --- | --- |
| **Anthropic Messages** | `POST /v1/messages` | Powers every station's `ProcessBucket` (Generator/Filter/Sorter/Checker reasoning + Orchestrator DAG-spec generation) and the chat subsystem (per-agent dialogue + rule updates). Default model `claude-sonnet-4-6`, `MaxTokens = 4096`. | `Content/Secrets/AnthropicAPIKey.txt` (preferred — auto-staged into packaged builds) or `Saved/AnthropicAPIKey.txt`. |
| **OpenAI Whisper** | `POST /v1/audio/transcriptions` | Push-to-talk speech → text. Multipart upload of M4A/AAC, `model=whisper-1`, `language=en`. | `Content/Secrets/OpenAIAPIKey.txt` or `Saved/OpenAIAPIKey.txt`. |
| **macOS `say`** | local fork/exec | Text → speech for every TTS line. | n/a — bundled with macOS. |

`UClaudeAPISubsystem::LoadAPIKey` and `UOpenAIAPISubsystem::LoadAPIKey`
both check `Saved/` then `Content/Secrets/` and log a `Display`-level
line on success. Story 29 (LogClaudeAPI) and Story 30 (LogOpenAI)
add per-call request/response tracing so you can reconstruct any
single PIE session's full LLM traffic from logs.

## Packaging a standalone build

**Editor:** *Platforms → Mac → Package Project →* pick an output folder.

The package contains:

- `<App>.app/Contents/UE/AssemblyLineSimul/Content/Secrets/AnthropicAPIKey.txt`
- `<App>.app/Contents/UE/AssemblyLineSimul/Content/Secrets/OpenAIAPIKey.txt`
- `<App>.app/Contents/UE/AssemblyLineSimul/Content/Agents/*.md` (the
  prompt bundle from Story 27 — must ship since `AgentPromptLibrary`
  reads them at runtime)

…all auto-staged via `+DirectoriesToAlwaysStageAsNonUFS` in
`Config/DefaultGame.ini`. **No manual key-copy step needed**, and
the sandboxed Mac container's writable `Saved/` dir is checked first
for both keys and the per-mission `.md` overrides.

Default GameMode is `BP_AssemblyLineGameMode` (set in
`Config/DefaultEngine.ini` so packaged builds use it).

### Post-stage fix-up: voice recognition (mandatory after every package)

`Config/DefaultEngine.ini` contains
`+ExtraPlistData=<key>NSMicrophoneUsageDescription</key>...` so the
mic-usage string *should* land in the packaged `Info.plist`
automatically — but UAT silently drops it in UE 5.7. Without that
key macOS denies mic access without ever prompting; AVAudioRecorder
records 3 s of silence; Whisper transcribes silence as `"you"`.

After every `RunUAT BuildCookRun` Mac package (or
*Platforms → Mac → Package Project*), run:

```bash
./Build/Mac/Scripts/fix_voice_in_packaged_app.sh
```

What it does:

1. Adds `NSMicrophoneUsageDescription` to the staged `Info.plist`.
2. Re-signs the bundle ad-hoc.

Optional `--reset-permission` resets macOS's mic permission cache for
the bundle id (`tccutil reset Microphone …`).

Once the script runs, launch the .app and grant mic permission when
prompted (or check **System Settings → Privacy & Security → Microphone**).
Voice should work.

## Known limitations / future work

- **Per-instance voice / chat routing not yet supported (Story 35
  documented limitation).** Multi-instance topologies (two Filters,
  two Sorters) spawn and run end-to-end; voice hails like *"Hey
  Filter"* still resolve to **Instance 0** via the backward-compat
  shim. Voicing *"Hey Filter Two"* to address Filter/1 distinctly is
  a future story (requires `VoiceHailParser` + `UVoiceSubsystem` to
  carry `FNodeRef` instead of `EStationType`).
- **Per-instance Orchestrator-authored Roles not yet supported (Story 35).**
  Both Filter/0 and Filter/1 share the same `Saved/Agents/Filter.md`
  Role written by the Orchestrator. Their `CurrentRule` differs (set
  per-node from the spec), but the personality prose is shared. Per-
  instance Saved files (`Saved/Agents/Filter_0.md` etc.) is a future
  story.
- **No green-flash mid-chain Checker verdict (Story 35 tradeoff).**
  When the Checker sits mid-chain (e.g., the 5-stage *"...check, then
  take only the best 2"* mission), PASS forwards silently to the
  successor. Auto-loop with a visible verdict cue would race the
  in-flight carrier against a fresh source-spawn, so for v1 mid-chain
  PASS is silent. The actual terminal still gets the green flash via
  Story 37's `CompleteCycle`.
- **Multi-terminal cycle dedup not yet supported (Story 37 tradeoff).**
  A spec with multiple terminals (e.g., a fan-out where both branches
  end in distinct terminals) fires `OnCycleCompleted` once per terminal
  that completes. Each schedules the auto-loop timer, which would
  spawn multiple Generator carriers simultaneously. No current mission
  shape exercises this. Future story adds a per-cycle "already
  completed" flag.
- **Chat history grows unbounded across re-missions.** The
  Orchestrator's history accumulates every prior mission text. After
  ~5 missions the prompt gets long enough to hit Claude latency
  spikes. A "trim oldest" or "summarize prior missions" pass would
  help.
- **No persistence** of the spawned topology across sessions.
- **Visual spawn animation is instant** — stations and workers pop
  in. A staged build animation (one station materializing per beat)
  would sell the moment more.
- **macOS only**: `UMacAudioCapture` is the only voice-capture
  backend; voice flow degrades gracefully on other platforms.
- **Packaged-build mic permission needs the post-stage script** —
  see [Post-stage fix-up](#post-stage-fix-up-voice-recognition-mandatory-after-every-package).
- **TTS race**: `SpeakResponse` writes to a single
  `agent_say_buffer.txt` file. Concurrent speakers can rarely clobber
  each other's input file. Fix: per-call unique filenames or
  `osascript`.
- **No rework cap**: the Director will keep dispatching rejected
  carriers back to the rework station as long as the Checker keeps
  rejecting. By design (the demo wants to show the agents trying
  again), but in production you'd want a hard limit.
- **Filter selection preview only** — Sorter and Checker don't have
  an analogous "what changed?" highlight. The Sorter could flash
  reorder arrows, the Checker could outline rule-violators in red.
- **Watermark GC / `Store` trait deferred.** Sketched in
  [Docs/DAG_Architecture.md](Docs/DAG_Architecture.md) but not
  implemented. Story 34's wholesale teardown handles cleanup
  satisfactorily for the demo's lifetimes.
