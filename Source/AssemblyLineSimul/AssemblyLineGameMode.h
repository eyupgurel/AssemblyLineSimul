#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AssemblyLineTypes.h"
#include "AssemblyLineGameMode.generated.h"

class USkeletalMesh;
class UStationTalkWidget;
class ABucket;

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

	// UMG widget class assigned to every spawned station's TalkWidgetClass.
	UPROPERTY(EditAnywhere, Category = "AssemblyLine")
	TSubclassOf<UStationTalkWidget> StationTalkWidgetClass;

	// Bucket class propagated to the Director so newly-spawned buckets adopt e.g. a
	// Blueprint subclass that has BilliardBallMaterial set.
	UPROPERTY(EditAnywhere, Category = "AssemblyLine")
	TSubclassOf<ABucket> BucketClass;

	// Per-station Working-state duration (seconds) applied to every spawned worker.
	// Slows the demo so the audience can absorb each operation; tests are unaffected
	// because they spawn workers directly without going through the GameMode.
	UPROPERTY(EditAnywhere, Category = "AssemblyLine")
	float StationWorkDuration = 20.f;

	// Spawns 4 stations + 4 workers + cinematic camera and registers them with the Director.
	// Public so tests can drive without running the full BeginPlay path.
	void SpawnAssemblyLine();

	// Spawns the ACinematicCameraDirector with a default 3-shot sequence (wide / mid / checker).
	// Public so tests can verify spawn without the full BeginPlay path.
	void SpawnCinematicDirector();

	// Spawns an AAssemblyLineFeedback actor that flashes lights on Checker accept/reject.
	void SpawnFeedback();

protected:
	virtual void BeginPlay() override;
};
