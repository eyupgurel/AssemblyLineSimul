#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AssemblyLineTypes.h"
#include "DAG/AssemblyLineDAG.h"  // FStationNode (spawn-spec input)
#include "AssemblyLineGameMode.generated.h"

class UStaticMesh;
class USkeletalMesh;
class UInputAction;
class UInputMappingContext;
class APayloadCarrier;
class UAgentChatSubsystem;
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

	// Bucket class propagated to the Director so newly-spawned buckets adopt e.g. a
	// Blueprint subclass that has BilliardBallMaterial set.
	UPROPERTY(EditAnywhere, Category = "AssemblyLine")
	TSubclassOf<APayloadCarrier> CarrierClass;

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

	// Story 32b — boot path. Spawns exactly one AOrchestratorStation and
	// registers it with the Director. No worker is spawned (the Orchestrator
	// is chat-only). The line itself is materialized later via
	// SpawnLineFromSpec when the Orchestrator returns a DAG.
	void SpawnOrchestrator();

	// Story 32b — mission path. Spawns one AStation (correct subclass per
	// node Kind) + one AWorkerRobot per node, applies each node's rule,
	// registers with the Director, and builds the DAG. Stations placed
	// along X at LineOrigin + idx * StationSpacing in spec iteration order.
	// Returns false (and spawns nothing) on: invalid DAG (cycle / unknown
	// type / undeclared parent), or a spec containing duplicate kinds (v1
	// constraint — see Story 32b AC32b.9).
	bool SpawnLineFromSpec(const TArray<FStationNode>& Nodes);

	// Spawns the FloorMesh under the line, centered + scaled by FloorScale.
	// No-op when FloorMesh is unset. Public for tests.
	void SpawnFloor();

	// Spawns the ACinematicCameraDirector with a default 3-shot sequence (wide / mid / checker).
	// Public so tests can verify spawn without the full BeginPlay path.
	void SpawnCinematicDirector();

	// Spawns an AAssemblyLineFeedback actor that flashes lights on Checker accept/reject.
	void SpawnFeedback();

	// Binds Space (hold) to push-to-talk: capture audio while held, transcribe via
	// Whisper on release, route the result through UVoiceSubsystem.
	void SetupVoiceInput();

	UFUNCTION() void OnVoiceTalkStarted();
	UFUNCTION() void OnVoiceTalkCompleted();
	UFUNCTION() void OnMissionKeyPressed();

	// Story 33a — file-driven Orchestrator kickoff. Loads the
	// `## Mission` section from `Content/Agents/Orchestrator.md` and
	// pushes it through UAgentChatSubsystem as a user message addressed
	// to the Orchestrator. The existing OnDAGProposed → spawn pipeline
	// (Story 32b) takes over from there. No-op (logs Warning) if the
	// Mission section is empty or the chat subsystem is unavailable.
	UFUNCTION(BlueprintCallable, Category = "AssemblyLine|Mission")
	void SendDefaultMission();

	// Story 33a — when true, BeginPlay schedules SendDefaultMission()
	// ~2 s after the Orchestrator spawns so the demo runs hands-free
	// (useful for recordings or environments where the mic is unreliable).
	// Off by default — voice path is the canonical operator entry.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AssemblyLine|Mission")
	bool bAutoMissionAtBoot = false;

	// Story 33a — test seam. AAssemblyLineGameMode normally resolves the
	// chat subsystem via GetGameInstance()->GetSubsystem<>(); tests can
	// inject a transient subsystem directly so they don't need a real
	// UGameInstance subsystem collection.
	void SetChatSubsystemForTesting(UAgentChatSubsystem* Chat) { ChatOverride = Chat; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() TObjectPtr<UInputAction>           VoiceTalkAction;
	UPROPERTY() TObjectPtr<UInputMappingContext>   VoiceMappingContext;
	UPROPERTY() TObjectPtr<UMacAudioCapture>       AudioCapture;

	// Story 33a — Enter-key binding for SendDefaultMission. Built lazily
	// in SetupVoiceInput so it shares the same Enhanced Input plumbing.
	UPROPERTY() TObjectPtr<UInputAction>           MissionAction;

	// Story 33a — test override (see SetChatSubsystemForTesting).
	UPROPERTY() TObjectPtr<UAgentChatSubsystem>    ChatOverride;

	// Wires UVoiceSubsystem::OnActiveAgentChanged → station glow + affirmation TTS.
	FDelegateHandle ActiveAgentChangedHandle;
	void HandleActiveAgentChanged(EStationType Agent);

public:
	// Story 33b — composes a full per-agent .md from Orchestrator-authored
	// Role + per-node Rule + the static ProcessBucketPrompt (and Checker
	// DerivedRuleTemplate) and writes it to Saved/Agents/<Kind>.md. Logs
	// the absolute path at Display level. Public for tests; production
	// code only invokes it from HandleDAGProposed.
	void WriteOrchestratorAuthoredPrompts(const TArray<FStationNode>& Nodes,
		const TMap<EStationType, FString>& PromptsByKind);

	// Story 34 — destroys every actor the previous mission spawned
	// (non-Orchestrator stations, all workers, all buckets, the cinematic
	// director, the feedback actor), wipes stale Saved/Agents/ files, and
	// resets Director state via ClearLineState. No-op when the world has
	// only the boot-time Orchestrator. Public for tests; production code
	// invokes it from HandleDAGProposed.
	void ClearExistingLine();

	// Story 34 — public for tests so they can drive the spawn pipeline
	// end-to-end (clear → write prompts → spawn → cinematic → feedback)
	// without going through the chat subsystem.
	void HandleDAGProposed(const TArray<FStationNode>& Nodes,
		const TMap<EStationType, FString>& PromptsByKind);

private:
	// Story 32b — handle for UAgentChatSubsystem::OnDAGProposed. Fired when
	// the Orchestrator's chat reply yields a parsed DAG; the handler runs
	// ClearExistingLine (Story 34) → WriteOrchestratorAuthoredPrompts
	// (Story 33b) → SpawnLineFromSpec → SpawnCinematicDirector →
	// SpawnFeedback → StartAllSourceCycles.
	FDelegateHandle DAGProposedHandle;
};
