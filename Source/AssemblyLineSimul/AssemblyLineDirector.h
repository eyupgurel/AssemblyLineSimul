#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AssemblyLineTypes.h"
#include "AssemblyLineDirector.generated.h"

class AStation;
class AWorkerRobot;
class ABucket;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyLineCycleCompleted, ABucket* /*Bucket*/);

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AssemblyLine")
	float DelayBetweenCycles = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AssemblyLine")
	bool bAutoLoop = true;

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
