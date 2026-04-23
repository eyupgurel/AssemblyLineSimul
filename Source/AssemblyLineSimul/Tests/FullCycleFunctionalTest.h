#pragma once

#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "FullCycleFunctionalTest.generated.h"

class ABucket;
class AStation;
class AWorkerRobot;

UCLASS()
class ASSEMBLYLINESIMUL_API AFullCycleFunctionalTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AFullCycleFunctionalTest();

	UPROPERTY(EditAnywhere, Category = "AssemblyLine")
	FVector LineOrigin = FVector(0.f, 0.f, 100.f);

	UPROPERTY(EditAnywhere, Category = "AssemblyLine")
	float StationSpacing = 1200.f;

protected:
	virtual void StartTest() override;

private:
	UPROPERTY() TArray<TObjectPtr<AStation>> SpawnedStations;
	UPROPERTY() TArray<TObjectPtr<AWorkerRobot>> SpawnedWorkers;

	FDelegateHandle CycleCompletedHandle;

	void HandleCycleCompleted(ABucket* Bucket);
};
