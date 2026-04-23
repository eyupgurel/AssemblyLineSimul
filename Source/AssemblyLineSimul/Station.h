#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AssemblyLineTypes.h"
#include "Station.generated.h"

class ABucket;
class UStaticMeshComponent;
class USceneComponent;
class UTextRenderComponent;

UCLASS(Abstract)
class ASSEMBLYLINESIMUL_API AStation : public AActor
{
	GENERATED_BODY()

public:
	AStation();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<USceneComponent> InputSlot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<USceneComponent> OutputSlot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<USceneComponent> WorkerStandPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<UTextRenderComponent> NameLabel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station")
	EStationType StationType = EStationType::Generator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ErrorRate = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station")
	FString DisplayName = TEXT("Station");

	virtual void ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete);

protected:
	virtual void BeginPlay() override;
};
