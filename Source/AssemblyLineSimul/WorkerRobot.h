#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "AssemblyLineTypes.h"
#include "WorkerRobot.generated.h"

class ABucket;
class AStation;
class UCapsuleComponent;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot")
	TObjectPtr<USceneComponent> CarrySocket;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot")
	TObjectPtr<UTextRenderComponent> StateLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot")
	float MoveSpeed = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot")
	float WorkDuration = 1.2f;

	UPROPERTY(BlueprintReadOnly, Category = "Robot")
	TObjectPtr<AStation> AssignedStation;

	// Result of the most recent ProcessBucket call (read by the Director after task completes).
	UPROPERTY(BlueprintReadOnly, Category = "Robot")
	FStationProcessResult LastResult;

	void AssignStation(AStation* Station);

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
