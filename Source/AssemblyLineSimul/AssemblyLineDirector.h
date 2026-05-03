#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AssemblyLineTypes.h"
#include "Bucket.h"  // ABucket complete type needed for TSubclassOf<ABucket>
#include "DAG/AssemblyLineDAG.h"
#include "AssemblyLineDirector.generated.h"

class AStation;
class AWorkerRobot;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyLineCycleCompleted, ABucket* /*Bucket*/);
DECLARE_MULTICAST_DELEGATE(FOnAssemblyLineCheckerStarted);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyLineCycleRejected, ABucket* /*Bucket*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyLineCycleRecycled, ABucket* /*Bucket*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyLineStationActive, EStationType /*StationType*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyLineStationIdle, EStationType /*StationType*/);

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
	AWorkerRobot* GetRobotForStation(EStationType Type) const;

	// Public accessor so the chat subsystem can update a station's CurrentRule when the
	// user instructs the agent. Returns nullptr if not registered.
	AStation* GetStationOfType(EStationType Type) const;

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
	TSubclassOf<ABucket> BucketClass = nullptr;

	// Public so unit specs can simulate a worker completion without spinning up a
	// full FSM (e.g. test the empty-bucket recycle path).
	void OnRobotDoneAt(EStationType Type, ABucket* Bucket);

	// Story 31a — register the line's topology. Per AC31a.6 dispatch routes
	// through this graph instead of a hardcoded EStationType chain. Returns
	// false on cycle / duplicate / unknown-parent (build failure already
	// logs the reason).
	bool BuildLineDAG(const TArray<FStationNode>& Nodes);

	// Story 31a — pure-domain DAG. Read access is needed by ACheckerStation to
	// walk ancestors when composing its derived rule (AC31a.5). Held by value;
	// no engine deps.
	const FAssemblyLineDAG& GetDAG() const { return DAG; }

private:
	UPROPERTY()
	TMap<EStationType, TObjectPtr<AStation>> StationByType;

	UPROPERTY()
	TMap<EStationType, TObjectPtr<AWorkerRobot>> RobotByStation;

	FAssemblyLineDAG DAG;

	// Story 31d — fan-in wait state. WaitingFor[Child] is the set of parents
	// not yet arrived for the current cycle; lazily re-populated from
	// DAG.GetParents(Child) on first arrival of each cycle. InboundBuckets
	// holds the queued parent buckets until all K arrive. TWeakObjectPtr is
	// GC-safe for the brief queue→fire window.
	TMap<FNodeRef, TSet<FNodeRef>>                  WaitingFor;
	TMap<FNodeRef, TArray<TWeakObjectPtr<ABucket>>> InboundBuckets;

	AStation* GetStation(EStationType Type) const;
	AWorkerRobot* GetRobot(EStationType Type) const;

	void DispatchToStation(EStationType Type, ABucket* Bucket, AStation* SourceStation);

	// Story 31d — if Child is a fan-in node (>1 parents in the DAG), queue
	// Bucket and update the wait set. Returns true if queued (caller must
	// NOT dispatch normally); false otherwise. Fires the merge inline when
	// the wait set drains to empty.
	bool QueueForFanInOrDispatch(const FNodeRef& Child, ABucket* Bucket, EStationType ParentType);

	// Story 31d — invoked by QueueForFanInOrDispatch when WaitingFor[Child]
	// drains. Calls Child's ProcessBucket with all queued inputs; on
	// OnComplete destroys Inputs[1..N-1] and continues the dispatch chain
	// from Inputs[0].
	void FireFanInMerge(const FNodeRef& Child);
};
