#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AssemblyLineTypes.h"
#include "Bucket.h"  // ABucket complete type needed for TSubclassOf<ABucket>
#include "AssemblyLineDirector.generated.h"

class AStation;
class AWorkerRobot;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyLineCycleCompleted, ABucket* /*Bucket*/);
DECLARE_MULTICAST_DELEGATE(FOnAssemblyLineCheckerStarted);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyLineCycleRejected, ABucket* /*Bucket*/);
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

private:
	UPROPERTY()
	TMap<EStationType, TObjectPtr<AStation>> StationByType;

	UPROPERTY()
	TMap<EStationType, TObjectPtr<AWorkerRobot>> RobotByStation;

	AStation* GetStation(EStationType Type) const;
	AWorkerRobot* GetRobot(EStationType Type) const;

	void DispatchToStation(EStationType Type, ABucket* Bucket, AStation* SourceStation);
	void OnRobotDoneAt(EStationType Type, ABucket* Bucket);
};
