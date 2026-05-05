# Story 38 — Payload + Visualizer abstraction (replaces `ABucket` + balls)

Drafted 2026-05-04. Carves the bucket+balls implementation into a
plug-and-play three-part abstraction: a generic carrier actor, a
payload component holding the typed data, and a visualizer component
that renders the payload as scene primitives. Stations cast to the
expected payload type at `ProcessBucket` entry. New agent kinds
(text, image, audio, document, geometry) become a matter of dropping
in a new `UPayload` subclass + `UPayloadVisualizer` subclass; the
runtime (Director, Worker, Cinematic, ChatSubsystem) doesn't need to
change.

## Story

> As a developer adding a new agent kind whose payload isn't an
> integer array (text agents, image agents, audio agents), I want to
> drop in a new `UPayload` + `UPayloadVisualizer` Blueprint pair and
> have the existing runtime carry it around without modification.

## Acceptance Criteria

- **AC38.1 — `UPayload` abstract base.** Pure-data UObject (no engine
  deps beyond UObject). Required virtuals: `int32 ItemCount() const`,
  `FString ToPromptString() const` (for embedding in Claude prompts),
  `UPayload* Clone(UObject* Outer) const` (deep copy for fan-out).
  Default `bool IsEmpty() const { return ItemCount() == 0; }`.
  `FOnPayloadChanged OnChanged` multicast delegate fires after any
  mutation — visualizer + cinematic subscribe.

- **AC38.2 — `UIntegerArrayPayload` concrete subclass.** Holds
  `TArray<int32> Items`. `ToPromptString` returns `[1, 2, 3]` format
  matching today's `ABucket::GetContentsString`. `Clone` deep-copies
  Items into a new payload owned by `Outer`.

- **AC38.3 — `UPayloadVisualizer` abstract base
  (USceneComponent).** Required virtuals: `BindPayload(UPayload*)`
  (subscribes to OnChanged → calls Rebuild), `Rebuild()` (re-render
  scene primitives from current payload), `HighlightItemsAtIndices(const TArray<int32>&)`
  + `ClearHighlight()`. `FOnVisualizationRevealed OnVisualizationRevealed`
  fires when a Rebuild transitions the visualization from empty/hidden
  to populated/visible — replaces today's `ABucket::OnContentsRevealed`.

- **AC38.4 — `UBilliardBallVisualizer` concrete component.** Owns
  the wireframe crate (12 emissive cylinder edges) + per-item billiard
  spheres + the `BilliardBallMaterial` MID. Lifts the rendering code
  currently in `ABucket::OnConstruction` + `RefreshContents` +
  `HighlightBallsAtIndices` verbatim into the component. Reads items
  from `Cast<UIntegerArrayPayload>(BoundPayload)->Items`.

- **AC38.5 — `APayloadCarrier` concrete actor.** Replaces `ABucket`.
  - `UPROPERTY(EditAnywhere) TSubclassOf<UPayload> PayloadClass;`
  - `UPROPERTY(EditAnywhere) TSubclassOf<UPayloadVisualizer> VisualizerClass;`
  - `UPROPERTY() TObjectPtr<UPayload> Payload;` (instantiated at
    OnConstruction from PayloadClass)
  - `UPROPERTY() TObjectPtr<UPayloadVisualizer> Visualizer;` (instantiated
    at OnConstruction from VisualizerClass; attached to RootComponent)
  - Pass-throughs: `GetContentsString()` → `Payload->ToPromptString()`;
    `HighlightItemsAtIndices(...)` → `Visualizer->HighlightItemsAtIndices(...)`;
    `CloneIntoWorld(World, Loc)` → spawns a fresh `APayloadCarrier`
    of the same Class, deep-clones Payload via `Payload->Clone`, the
    new visualizer auto-binds and rebuilds on OnChanged.

- **AC38.6 — Default carrier preset.** A `BP_NumberCarrier` Blueprint
  (or set the Director's `CarrierClass` C++ default) wires
  `PayloadClass = UIntegerArrayPayload`,
  `VisualizerClass = UBilliardBallVisualizer`. This is the carrier
  used for every cycle in the current 4-station and 5-station
  missions — visual + data behavior is byte-identical to today.

- **AC38.7 — `ABucket` is removed.** All references in C++ refactor to
  `APayloadCarrier`. The existing `BP_BilliardBucket` Blueprint asset
  is updated to inherit from `APayloadCarrier` (post-merge editor
  step) with PayloadClass + VisualizerClass set to the integer-array
  + billiard-ball pair. Director's `BucketClass` property renames to
  `CarrierClass`.

- **AC38.8 — Station signature change.** `AStation::ProcessBucket(const TArray<ABucket*>&, FStationProcessComplete)`
  becomes `AStation::ProcessBucket(const TArray<APayloadCarrier*>&, FStationProcessComplete)`.
  All four production stations cast `Inputs[0]->Payload` to
  `UIntegerArrayPayload` at entry; on payload-type mismatch, log
  `Warning` and bail (cycle continues). Setters call `P->Items = ...; P->OnChanged.Broadcast();`
  to drive the visualizer.

- **AC38.9 — Runtime rename: `Bucket` → `Carrier` everywhere
  user-facing.** `OnRobotDoneAt(FNodeRef, ABucket*)` →
  `OnRobotDoneAt(FNodeRef, APayloadCarrier*)`. `Worker::CurrentBucket`
  → `Worker::CurrentCarrier`. `WorkerRobot::BeginTask(ABucket*, ...)`
  → `BeginTask(APayloadCarrier*, ...)`. `ChatSubsystem::GetCurrentBucketContents`
  → `GetCurrentCarrierContents`. `AssemblyLineFeedback::SpawnFlash(ABucket*, ...)`
  → `SpawnFlash(APayloadCarrier*, ...)`. Director's `BucketClass`
  field → `CarrierClass`. Cinematic's `GetChaseTarget` returns
  `APayloadCarrier*`.

- **AC38.10 — Test ergonomics.** New helper in `TestPayloads.h`:
  ```cpp
  APayloadCarrier* MakeNumberCarrier(UWorld* World, FVector Loc, TArray<int32> Items);
  ```
  Tests that previously did
  `ABucket* B = SpawnActor<ABucket>(...); B->Contents = {1,2,3};`
  become
  `APayloadCarrier* C = MakeNumberCarrier(World, Loc, {1,2,3});`.

- **AC38.11 — Tests** (replacing or extending current bucket-touching
  specs):
  - `IntegerArrayPayloadSpec` (new): `ItemCount`, `IsEmpty`,
    `ToPromptString` returns `[1, 2, 3]` for `{1,2,3}`, `Clone`
    produces an independent payload with copied Items, OnChanged
    broadcasts after mutation.
  - `BilliardBallVisualizerSpec` (new): `BindPayload` subscribes to
    OnChanged; `Rebuild` creates one ball per item; `HighlightItemsAtIndices`
    paints only the named indices; OnVisualizationRevealed fires on
    empty→non-empty transition.
  - `PayloadCarrierSpec` (new, mostly migrated from `BucketSpec`):
    `OnConstruction` instantiates Payload + Visualizer from class
    properties; `GetContentsString` delegates to Payload;
    `HighlightItemsAtIndices` delegates to Visualizer; `CloneIntoWorld`
    spawns a distinct actor with cloned Payload.
  - `BucketSpec` → deleted (functionality covered by the three new specs).
  - All other specs (Director, GameMode, Worker, Cinematic, Station,
    StationSubclasses, FullCycle) updated to use `MakeNumberCarrier`
    + `APayloadCarrier*` pointers; assertions on `Contents` rewritten
    to `Cast<UIntegerArrayPayload>(C->Payload)->Items`.

## Out of scope (deferred)

- **A second concrete payload type** (text, image, audio). The
  abstraction makes them possible; actually using one is a future
  story when a non-numeric agent kind is required. Avoiding speculative
  abstraction-by-need.
- **Multi-payload missions** (different payload types in the same
  DAG). Today every cycle uses one carrier class; multi-payload
  needs a per-source carrier class on the Director or per-node
  carrier class in the Orchestrator's spec.
- **Payload-type validation at DAG-build time.** Today a Filter that
  receives a non-integer payload will `Warning`-log at first
  ProcessBucket call. A future story could let the Orchestrator
  declare per-node payload types and validate the spec early.
- **Orchestrator authoring carrier visualization choices** ("for this
  mission, render numbers as a histogram instead of billiard balls").
  Visualizer is a designer-time choice in v1.

## Implementation notes (non-contract)

- Order of operations for the migration:
  1. Add `UPayload` + `UIntegerArrayPayload` + `UPayloadVisualizer` +
     `UBilliardBallVisualizer` (alongside existing `ABucket`).
  2. Add `APayloadCarrier` (alongside existing `ABucket`).
  3. Migrate `Station::ProcessBucket` signature + all four station
     subclasses + Director + Worker + Cinematic + Feedback +
     ChatSubsystem in one pass — these are coupled.
  4. Delete `Bucket.{h,cpp}`.
  5. Rename `BucketClass` → `CarrierClass` on Director + GameMode.
  6. Update tests; introduce `MakeNumberCarrier` helper.
  7. PIE check: post-merge, the user reparents `BP_BilliardBucket`
     in the editor to `APayloadCarrier` and sets PayloadClass +
     VisualizerClass.

- `UBilliardBallVisualizer` keeps the `BilliardBallMaterial` /
  `BallRelativeRotation` / `GlassTint` UPROPERTYs that were on
  `ABucket`. Designer reconfigures via the visualizer component on
  the Blueprint subclass.

- The visualizer's USceneComponent attachment: spawned at carrier's
  `OnConstruction` via `NewObject<UPayloadVisualizer>(this, VisualizerClass)`
  + `RegisterComponent` + `AttachToComponent(RootComponent, ...)`.

- Payload OnChanged → Visualizer Rebuild wiring: the visualizer
  subscribes inside its `BindPayload` impl. Carrier's
  `OnConstruction` calls `Visualizer->BindPayload(Payload)` after
  both are instantiated.

- Carrier `CloneIntoWorld` spawns a new actor of the same Class
  (preserves Blueprint defaults), then BEFORE auto-instantiation runs
  it overwrites the spawned-default Payload with a `Payload->Clone(NewActor)`.
  The Visualizer is fresh per carrier (presentation is not part of
  the data); auto-instantiation handles that.
