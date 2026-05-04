#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "AssemblyLineTypes.h"
#include "DAG/AssemblyLineDAG.h"  // FNodeRef on phase events (Story 36)
#include "WorkerRobot.generated.h"

class ABucket;
class AStation;
class UCapsuleComponent;
class UPointLightComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UStaticMeshComponent;
class USceneComponent;

UENUM(BlueprintType)
enum class EWorkerState : uint8
{
	Idle,
	MoveToInput,
	PickUp,
	MoveToWorkPos,
	Working,
	MoveToOutput,
	Place,
	ReturnHome
};

DECLARE_DELEGATE_OneParam(FWorkerTaskComplete, ABucket* /*Bucket*/);
// Story 36 — phase events carry the assigned station's full FNodeRef so
// downstream listeners (cinematic camera, multi-instance HUD, etc.) can
// distinguish Filter/0 from Filter/1. Pre-Story 36 these were
// EStationType-typed and the cinematic was multi-instance-blind.
DECLARE_MULTICAST_DELEGATE_OneParam(FOnWorkerPhase, const FNodeRef& /*Ref*/);
// Aliases for clarity at the new fire sites (Working entry / Working exit).
using FOnWorkerStartedWorking = FOnWorkerPhase;
using FOnWorkerFinishedWorking = FOnWorkerPhase;

UCLASS()
class ASSEMBLYLINESIMUL_API AWorkerRobot : public APawn
{
	GENERATED_BODY()

public:
	AWorkerRobot();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot")
	TObjectPtr<UCapsuleComponent> CapsuleComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot")
	TObjectPtr<UStaticMeshComponent> HeadMesh;

	// Replaces the placeholder body+head primitives when ApplyBodyMesh is called with a non-null mesh.
	UPROPERTY(VisibleAnywhere, Category = "Robot")
	TObjectPtr<USkeletalMeshComponent> SkeletalBodyMesh;

	// Composite mech-style body assembled from engine BasicShape meshes; visible by default.
	UPROPERTY(VisibleAnywhere, Category = "Robot|Body")
	TObjectPtr<UStaticMeshComponent> Torso;

	UPROPERTY(VisibleAnywhere, Category = "Robot|Body")
	TObjectPtr<UStaticMeshComponent> HeadDome;

	UPROPERTY(VisibleAnywhere, Category = "Robot|Body")
	TObjectPtr<UStaticMeshComponent> Eye;

	UPROPERTY(VisibleAnywhere, Category = "Robot|Body")
	TObjectPtr<UStaticMeshComponent> LeftArm;

	UPROPERTY(VisibleAnywhere, Category = "Robot|Body")
	TObjectPtr<UStaticMeshComponent> RightArm;

	UPROPERTY(VisibleAnywhere, Category = "Robot|Body")
	TObjectPtr<UStaticMeshComponent> Antenna;

	// Asset to load via LoadAndApplyBodyMesh; the GameMode propagates its WorkerRobotMeshAsset here.
	UPROPERTY()
	TSoftObjectPtr<USkeletalMesh> BodyMeshAsset;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot")
	TObjectPtr<USceneComponent> CarrySocket;

	// Green point light that lights when this worker's agent is the active voice
	// speaker (Story 19). Off by default; toggled by SetActive.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot")
	TObjectPtr<UPointLightComponent> ActiveLight;

	// Toggle the green active-speaker glow on this worker.
	void SetActive(bool bActive);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot")
	float MoveSpeed = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot")
	float WorkDuration = 3.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Robot")
	TObjectPtr<AStation> AssignedStation;

	// Result of the most recent ProcessBucket call (read by the Director after task completes).
	UPROPERTY(BlueprintReadOnly, Category = "Robot")
	FStationProcessResult LastResult;

	// Fires when this worker enters the PickUp state, with the station's type as payload.
	FOnWorkerPhase OnPickedUp;

	// Fires when this worker enters the Place state, with the station's type as payload.
	FOnWorkerPhase OnPlaced;

	// Fires when this worker enters the Working state (after walking to its work pos).
	// Cinematic uses this — closeup reserved for the actual processing moment, not the carry.
	FOnWorkerStartedWorking OnStartedWorking;

	// Fires when this worker leaves the Working state (about to MoveToOutput).
	FOnWorkerFinishedWorking OnFinishedWorking;

	void AssignStation(AStation* Station);

	// Public accessor for the bucket the worker is currently carrying / processing.
	ABucket* GetCurrentBucket() const { return CurrentBucket; }

	// Assigns ResolvedMesh to SkeletalBodyMesh and hides placeholders. Null is a no-op.
	void ApplyBodyMesh(USkeletalMesh* ResolvedMesh);

	// Synchronously loads BodyMeshAsset and calls ApplyBodyMesh.
	void LoadAndApplyBodyMesh();

	// Applies a per-instance tint color to the active body material.
	void ApplyTint(const FLinearColor& Color);

	// Order this robot to fetch a bucket from FromSlot, run their station's work,
	// place the result at ToSlot, then return home.
	void BeginTask(ABucket* Bucket, USceneComponent* FromSlot, USceneComponent* ToSlot, FWorkerTaskComplete OnComplete);

	// Idle pose played in stationary states (Idle, Working, PickUp, Place).
	UPROPERTY(VisibleAnywhere, Category = "Robot|Animation")
	TObjectPtr<class UAnimSequence> IdleAnimation;

	// Walk loop played in moving states (MoveToInput, MoveToWorkPos, MoveToOutput, ReturnHome).
	UPROPERTY(VisibleAnywhere, Category = "Robot|Animation")
	TObjectPtr<class UAnimSequence> WalkAnimation;

	// Test seam — production callers go through BeginTask; tests drive specific
	// state transitions to assert the animation swap contract.
	void EnterStateForTesting(EWorkerState NewState) { EnterState(NewState); }

	// Pure picker — returns IdleAnimation for stationary states, WalkAnimation
	// for moving states (MoveToInput / MoveToWorkPos / MoveToOutput / ReturnHome).
	// Public so tests can assert the contract without requiring a real
	// USkeletalMesh (GetSingleNodeInstance() returns null when the component
	// has no mesh, even after SetAnimation is called).
	UAnimSequence* PickAnimationForState(EWorkerState QueryState) const;

	virtual void Tick(float DeltaSeconds) override;

protected:
	EWorkerState State = EWorkerState::Idle;
	TObjectPtr<ABucket> CurrentBucket;
	TWeakObjectPtr<USceneComponent> FromSlotPtr;
	TWeakObjectPtr<USceneComponent> ToSlotPtr;
	FVector TargetLocation = FVector::ZeroVector;
	FVector HomeLocation = FVector::ZeroVector;
	float WorkTimer = 0.f;
	FWorkerTaskComplete TaskCompleteCb;

	void EnterState(EWorkerState NewState);
	bool MoveToward(const FVector& Target, float DeltaSeconds);
	void AttachBucket();
	void DetachBucketAt(USceneComponent* Slot);
	void RefreshAnimationForState();
};
