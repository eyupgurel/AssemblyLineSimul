#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AssemblyLineTypes.h"
#include "CinematicCameraDirector.generated.h"

class ABucket;
class ACameraActor;
class UAssemblyLineDirector;
class UInputAction;
class UInputMappingContext;

USTRUCT(BlueprintType)
struct FCinematicShot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Shot")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Shot")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, Category = "Shot")
	float FieldOfView = 85.f;

	UPROPERTY(EditAnywhere, Category = "Shot")
	float HoldDuration = 5.f;

	UPROPERTY(EditAnywhere, Category = "Shot")
	float BlendDuration = 1.5f;
};

UCLASS()
class ASSEMBLYLINESIMUL_API ACinematicCameraDirector : public AActor
{
	GENERATED_BODY()

public:
	ACinematicCameraDirector();

	UPROPERTY(EditAnywhere, Category = "Cinematic")
	TArray<FCinematicShot> Shots;

	UPROPERTY(EditAnywhere, Category = "Cinematic")
	bool bLoop = true;

	UPROPERTY(EditAnywhere, Category = "Cinematic")
	int32 CheckerShotIndex = 2;

	UPROPERTY(EditAnywhere, Category = "Cinematic")
	int32 ResumeShotIndex = 0;

	// Maps a station type to the shot index the camera should jump to when that station becomes active.
	UPROPERTY(EditAnywhere, Category = "Cinematic")
	TMap<EStationType, int32> StationCloseupShotIndex;

	void Start();
	void Stop();
	void AdvanceShot();
	void JumpToShot(int32 NewIndex);
	int32 GetCurrentShotIndex() const { return CurrentShotIndex; }

	// Subscribe to AssemblyLineDirector's checker / cycle events. Idempotent.
	void BindToAssemblyLine(UAssemblyLineDirector* Director);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	int32 CurrentShotIndex = 0;
	FTimerHandle ShotTimer;

	UPROPERTY()
	TArray<TObjectPtr<ACameraActor>> ShotCameras;

	UPROPERTY()
	TWeakObjectPtr<UAssemblyLineDirector> BoundDirector;

	FDelegateHandle CheckerStartedHandle;
	FDelegateHandle CycleCompletedHandle;
	FDelegateHandle CycleRejectedHandle;
	FDelegateHandle StationActiveHandle;
	FDelegateHandle StationIdleHandle;

	UPROPERTY()
	TObjectPtr<UInputMappingContext> SkipMappingContext;

	UPROPERTY()
	TObjectPtr<UInputAction> SkipAction;

	void ApplyShot(int32 Index);
	void ScheduleNextAdvance();
	void EnsureShotCameras();
	void HandleCheckerStarted();
	void HandleCycleResumed(ABucket* Bucket);
	void HandleStationActive(EStationType StationType);
	void HandleStationIdle(EStationType StationType);
	void HandleSkipPressed();
	void SetupSkipBinding();
};
