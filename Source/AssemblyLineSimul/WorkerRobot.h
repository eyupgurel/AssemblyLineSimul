#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "AssemblyLineTypes.h"
#include "WorkerRobot.generated.h"

class ABucket;
class AStation;
class UCapsuleComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UStaticMeshComponent;
class USceneComponent;
class UTextRenderComponent;

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
DECLARE_MULTICAST_DELEGATE_OneParam(FOnWorkerPhase, EStationType /*StationType*/);
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot")
	TObjectPtr<UTextRenderComponent> StateLabel;

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
	void UpdateLabel();
	bool MoveToward(const FVector& Target, float DeltaSeconds);
	void AttachBucket();
	void DetachBucketAt(USceneComponent* Slot);
};
