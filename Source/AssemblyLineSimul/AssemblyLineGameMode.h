#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AssemblyLineTypes.h"
#include "AssemblyLineGameMode.generated.h"

class USkeletalMesh;

UCLASS()
class ASSEMBLYLINESIMUL_API AAssemblyLineGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAssemblyLineGameMode();

	// Distance between station origins along the line.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AssemblyLine")
	float StationSpacing = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AssemblyLine")
	FVector LineOrigin = FVector(0.f, 0.f, 100.f);

	// Skeletal mesh applied to every spawned worker. If unset, workers keep their placeholder primitives.
	UPROPERTY(EditAnywhere, Category = "AssemblyLine")
	TSoftObjectPtr<USkeletalMesh> WorkerRobotMeshAsset;

	// Per-station tint applied to the worker's body material.
	UPROPERTY(EditAnywhere, Category = "AssemblyLine")
	TMap<EStationType, FLinearColor> RobotTintByStation;

	// Spawns 4 stations + 4 workers + cinematic camera and registers them with the Director.
	// Public so tests can drive without running the full BeginPlay path.
	void SpawnAssemblyLine();

	// Spawns the ACinematicCameraDirector with a default 3-shot sequence (wide / mid / checker).
	// Public so tests can verify spawn without the full BeginPlay path.
	void SpawnCinematicDirector();

protected:
	virtual void BeginPlay() override;
};
