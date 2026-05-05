#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AssemblyLineFeedback.generated.h"

class APayloadCarrier;
class UAssemblyLineDirector;

UCLASS()
class ASSEMBLYLINESIMUL_API AAssemblyLineFeedback : public AActor
{
	GENERATED_BODY()

public:
	AAssemblyLineFeedback();

	UPROPERTY(EditAnywhere, Category = "Feedback")
	FLinearColor RejectColor = FLinearColor(1.f, 0.1f, 0.05f, 1.f);

	UPROPERTY(EditAnywhere, Category = "Feedback")
	FLinearColor AcceptColor = FLinearColor(0.1f, 1.f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, Category = "Feedback")
	float RejectLifetime = 0.8f;

	UPROPERTY(EditAnywhere, Category = "Feedback")
	float AcceptLifetime = 1.2f;

	UPROPERTY(EditAnywhere, Category = "Feedback")
	float LightIntensity = 30000.f;

	UPROPERTY(EditAnywhere, Category = "Feedback")
	float LightAttenuationRadius = 600.f;

	void BindToAssemblyLine(UAssemblyLineDirector* Director);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY()
	TWeakObjectPtr<UAssemblyLineDirector> BoundDirector;

	FDelegateHandle CycleCompletedHandle;
	FDelegateHandle CycleRejectedHandle;

	void HandleCycleCompleted(APayloadCarrier* Bucket);
	void HandleCycleRejected(APayloadCarrier* Bucket);
	void SpawnFlash(APayloadCarrier* Bucket, const FLinearColor& Color, float Lifetime);
};
