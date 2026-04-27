#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AssemblyLineTypes.h"
#include "AssemblyLineGameMode.generated.h"

class UStaticMesh;
class USkeletalMesh;
class UStationTalkWidget;
class UAgentChatWidget;
class UInputAction;
class UInputMappingContext;
class ABucket;
class UMacAudioCapture;

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
	// Tests spawn workers directly and are unaffected.
	UPROPERTY(EditAnywhere, Category = "AssemblyLine")
	float StationWorkDuration = 5.f;

	// Story 20 — industrial floor under the line. If unset, no floor is spawned
	// (test environments and projects without the asset pack stay unaffected).
	// Production BP_AssemblyLineGameMode assigns SM_Metallic_Floor here.
	UPROPERTY(EditAnywhere, Category = "AssemblyLine|Floor")
	TSoftObjectPtr<UStaticMesh> FloorMesh;

	// Per-tile scale. Default 1 means each tile is the mesh's native size with
	// no UV stretching. Bump uniformly if your source mesh is too small.
	UPROPERTY(EditAnywhere, Category = "AssemblyLine|Floor")
	FVector FloorScale = FVector(1.f, 1.f, 1.f);

	// Big fixed grid centered on the line. Defaults overshoot every cinematic
	// shot so the ground is invisible under the floor in every angle. Bump
	// higher if you ever zoom out further.
	UPROPERTY(EditAnywhere, Category = "AssemblyLine|Floor", meta = (ClampMin = "1"))
	int32 FloorTilesX = 60;

	UPROPERTY(EditAnywhere, Category = "AssemblyLine|Floor", meta = (ClampMin = "1"))
	int32 FloorTilesY = 60;

	// Added to the computed floor center (worker-feet level under the line).
	// Default lifts the grid 85 cm so it sits flush with the station cube
	// bottoms instead of dangling 85 cm below them.
	UPROPERTY(EditAnywhere, Category = "AssemblyLine|Floor")
	FVector FloorOffset = FVector(0.f, 0.f, 85.f);

	// Spawns 4 stations + 4 workers + cinematic camera and registers them with the Director.
	// Public so tests can drive without running the full BeginPlay path.
	void SpawnAssemblyLine();

	// Spawns the FloorMesh under the line, centered + scaled by FloorScale.
	// No-op when FloorMesh is unset. Public for tests.
	void SpawnFloor();

	// Spawns the ACinematicCameraDirector with a default 3-shot sequence (wide / mid / checker).
	// Public so tests can verify spawn without the full BeginPlay path.
	void SpawnCinematicDirector();

	// Spawns an AAssemblyLineFeedback actor that flashes lights on Checker accept/reject.
	void SpawnFeedback();

	// Spawns the chat widget, hides it, and binds Tab to toggle visibility / focus.
	void SpawnChatWidget();

	// Binds Space (hold) to push-to-talk: capture audio while held, transcribe via
	// Whisper on release, route the result through UVoiceSubsystem.
	void SetupVoiceInput();

	UFUNCTION() void ToggleChatWidget();
	UFUNCTION() void OnVoiceTalkStarted();
	UFUNCTION() void OnVoiceTalkCompleted();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() TObjectPtr<UAgentChatWidget>       ChatWidget;
	UPROPERTY() TObjectPtr<UInputAction>           ChatToggleAction;
	UPROPERTY() TObjectPtr<UInputMappingContext>   ChatToggleMappingContext;
	UPROPERTY() TObjectPtr<UInputAction>           VoiceTalkAction;
	UPROPERTY() TObjectPtr<UInputMappingContext>   VoiceMappingContext;
	UPROPERTY() TObjectPtr<UMacAudioCapture>       AudioCapture;

	// Wires UVoiceSubsystem::OnActiveAgentChanged → station glow + affirmation TTS.
	FDelegateHandle ActiveAgentChangedHandle;
	void HandleActiveAgentChanged(EStationType Agent);
};
