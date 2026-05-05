#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AssemblyLineTypes.h"
#include "PayloadCarrier.h"  // APayloadCarrier complete type needed for TSubclassOf
#include "DAG/AssemblyLineDAG.h"
#include "AssemblyLineDirector.generated.h"

class AStation;
class AWorkerRobot;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyLineCycleCompleted, APayloadCarrier* /*Bucket*/);
DECLARE_MULTICAST_DELEGATE(FOnAssemblyLineCheckerStarted);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyLineCycleRejected, APayloadCarrier* /*Bucket*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyLineCycleRecycled, APayloadCarrier* /*Bucket*/);
// Story 36 — re-broadcast the worker's full FNodeRef so the cinematic
// camera (and any other listener) can distinguish multi-instance Filters
// of the same Kind. Pre-Story 36 this was OneParam<EStationType>.
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyLineStationActive, const FNodeRef& /*Ref*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyLineStationIdle, const FNodeRef& /*Ref*/);

UCLASS()
class ASSEMBLYLINESIMUL_API UAssemblyLineDirector : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterStation(AStation* Station);
	void RegisterRobot(AWorkerRobot* Robot);

	// Start a fresh cycle: spawn an empty bucket, dispatch Generator robot.
	UFUNCTION(BlueprintCallable, Category = "AssemblyLine")
	void StartCycle();

	// Story 32b — generalizes StartCycle for arbitrary spawned topologies.
	// Walks DAG.GetSourceNodes() and dispatches an empty bucket to each.
	// In a single-source line this collapses to one StartCycle call; in a
	// fan-in topology it kicks both source branches simultaneously so the
	// merge gate fires once both arrive.
	UFUNCTION(BlueprintCallable, Category = "AssemblyLine")
	void StartAllSourceCycles();

	// Fires when the Checker accepts a bucket (i.e. one full cycle finished without rework).
	FOnAssemblyLineCycleCompleted OnCycleCompleted;

	// Fires when a bucket is dispatched to the Checker station for inspection.
	FOnAssemblyLineCheckerStarted OnCheckerStarted;

	// Fires when the Checker rejects a bucket (after which it gets sent back to a prior station).
	FOnAssemblyLineCycleRejected OnCycleRejected;

	// Fires when a station's ProcessBucket left the bucket empty (e.g. Filter eliminated
	// every item during rework). Director destroys the bucket and starts a fresh cycle.
	// Story 17 AC17.7.
	FOnAssemblyLineCycleRecycled OnCycleRecycled;

	// Public accessor for the cinematic to introspect a station's worker (and therefore its
	// CurrentBucket). Returns nullptr if the station type isn't registered.
	// Story 35 — backward-compat shim: returns the Instance 0 worker for that Kind.
	// Multi-instance specs need GetRobotByNodeRef.
	AWorkerRobot* GetRobotForStation(EStationType Type) const;

	// Public accessor so the chat subsystem can update a station's CurrentRule when the
	// user instructs the agent. Returns nullptr if not registered.
	// Story 35 — backward-compat shim: returns the Instance 0 station for that Kind.
	// Multi-instance specs need GetStationByNodeRef.
	AStation* GetStationOfType(EStationType Type) const;

	// Story 35 — canonical lookups for multi-instance specs. The shims above
	// hardcode Instance 0; these accept any (Kind, Instance) pair.
	AStation*     GetStationByNodeRef(const FNodeRef& Ref) const;
	AWorkerRobot* GetRobotByNodeRef  (const FNodeRef& Ref) const;

	// Fires when a registered worker enters the PickUp phase at its station.
	FOnAssemblyLineStationActive OnStationActive;

	// Fires when a registered worker enters the Place phase at its station.
	FOnAssemblyLineStationIdle OnStationIdle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AssemblyLine")
	float DelayBetweenCycles = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AssemblyLine")
	bool bAutoLoop = true;

	// Class spawned for each new bucket; override with a Blueprint subclass to set
	// BilliardBallMaterial or other defaults.
	UPROPERTY(EditAnywhere, Category = "AssemblyLine")
	TSubclassOf<APayloadCarrier> CarrierClass = nullptr;

	// Public so unit specs can simulate a worker completion without spinning up a
	// full FSM (e.g. test the empty-bucket recycle path).
	// Story 35 — backward-compat shim. Equivalent to OnRobotDoneAt(FNodeRef{Type, 0}, Bucket).
	void OnRobotDoneAt(EStationType Type, APayloadCarrier* Bucket);

	// Story 35 — canonical FNodeRef-aware completion entry. Multi-instance
	// specs route here so dispatch consults the correct (Kind, Instance)
	// successors rather than always Instance 0.
	void OnRobotDoneAt(const FNodeRef& Ref, APayloadCarrier* Bucket);

	// Story 31a — register the line's topology. Per AC31a.6 dispatch routes
	// through this graph instead of a hardcoded EStationType chain. Returns
	// false on cycle / duplicate / unknown-parent (build failure already
	// logs the reason).
	bool BuildLineDAG(const TArray<FStationNode>& Nodes);

	// Story 31a — pure-domain DAG. Read access is needed by ACheckerStation to
	// walk ancestors when composing its derived rule (AC31a.5). Held by value;
	// no engine deps.
	const FAssemblyLineDAG& GetDAG() const { return DAG; }

	// Story 34 — re-missioning teardown. Empties StationByType (except the
	// Orchestrator entry), RobotByStation, WaitingFor, InboundBuckets;
	// resets the held DAG to empty; cancels every timer the Director
	// scheduled via TimerManager::ClearAllTimersForObject(this). Called by
	// AAssemblyLineGameMode::ClearExistingLine before SpawnLineFromSpec
	// re-builds.
	UFUNCTION(BlueprintCallable, Category = "AssemblyLine")
	void ClearLineState();

private:
	// Story 35 — rekeyed from EStationType to FNodeRef for multi-instance support.
	// One spec node = one entry. Instance 0 is the conventional "default" used by
	// the EStationType-keyed backward-compat shims (GetStationOfType etc.).
	UPROPERTY()
	TMap<FNodeRef, TObjectPtr<AStation>> StationByNodeRef;

	UPROPERTY()
	TMap<FNodeRef, TObjectPtr<AWorkerRobot>> RobotByNodeRef;

	// Story 35 — per-Kind monotonic counter for auto-assigning Instance on
	// RegisterStation. Reset by ClearLineState.
	TMap<EStationType, int32> NextInstanceByKind;

	FAssemblyLineDAG DAG;

	// Story 31d — fan-in wait state. WaitingFor[Child] is the set of parents
	// not yet arrived for the current cycle; lazily re-populated from
	// DAG.GetParents(Child) on first arrival of each cycle. InboundBuckets
	// holds the queued parent buckets until all K arrive. TWeakObjectPtr is
	// GC-safe for the brief queue→fire window.
	TMap<FNodeRef, TSet<FNodeRef>>                  WaitingFor;
	TMap<FNodeRef, TArray<TWeakObjectPtr<APayloadCarrier>>> InboundBuckets;

	// Story 35 — internal lookups now NodeRef-keyed. The EStationType
	// helpers below are convenience wrappers over Instance 0.
	AStation*     GetStation(const FNodeRef& Ref) const;
	AWorkerRobot* GetRobot  (const FNodeRef& Ref) const;

	void DispatchToStation(const FNodeRef& Target, APayloadCarrier* Bucket, AStation* SourceStation);

	// Story 37 — broadcasts OnCycleCompleted and (if bAutoLoop) schedules
	// the recycle-and-restart timer. Used by both the Checker-terminal PASS
	// branch and the "any registered terminal" branch so the same boilerplate
	// doesn't get duplicated.
	void CompleteCycle(APayloadCarrier* Bucket);

	// Story 31d — if Child is a fan-in node (>1 parents in the DAG), queue
	// Bucket and update the wait set. Returns true if queued (caller must
	// NOT dispatch normally); false otherwise. Fires the merge inline when
	// the wait set drains to empty.
	// Story 35 — ParentRef now FNodeRef so multi-instance fan-in works
	// (e.g., Filter/0 and Filter/1 both feeding into a Sorter).
	bool QueueForFanInOrDispatch(const FNodeRef& Child, APayloadCarrier* Bucket, const FNodeRef& ParentRef);

	// Story 31d — invoked by QueueForFanInOrDispatch when WaitingFor[Child]
	// drains. Calls Child's ProcessBucket with all queued inputs; on
	// OnComplete destroys Inputs[1..N-1] and continues the dispatch chain
	// from Inputs[0].
	void FireFanInMerge(const FNodeRef& Child);
};
