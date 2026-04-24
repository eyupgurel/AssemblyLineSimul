#include "AssemblyLineFeedback.h"
#include "AssemblyLineDirector.h"
#include "Bucket.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "TimerManager.h"

AAssemblyLineFeedback::AAssemblyLineFeedback()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AAssemblyLineFeedback::BindToAssemblyLine(UAssemblyLineDirector* Director)
{
	if (UAssemblyLineDirector* Old = BoundDirector.Get())
	{
		Old->OnCycleCompleted.Remove(CycleCompletedHandle);
		Old->OnCycleRejected.Remove(CycleRejectedHandle);
	}
	BoundDirector = Director;
	if (!Director) return;
	CycleCompletedHandle = Director->OnCycleCompleted.AddUObject(this, &AAssemblyLineFeedback::HandleCycleCompleted);
	CycleRejectedHandle  = Director->OnCycleRejected .AddUObject(this, &AAssemblyLineFeedback::HandleCycleRejected);
}

void AAssemblyLineFeedback::HandleCycleCompleted(ABucket* Bucket)
{
	SpawnFlash(Bucket, AcceptColor, AcceptLifetime);
}

void AAssemblyLineFeedback::HandleCycleRejected(ABucket* Bucket)
{
	SpawnFlash(Bucket, RejectColor, RejectLifetime);
}

void AAssemblyLineFeedback::SpawnFlash(ABucket* Bucket, const FLinearColor& Color, float Lifetime)
{
	if (!Bucket) return;
	UWorld* W = GetWorld();
	if (!W) return;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;
	APointLight* Light = W->SpawnActor<APointLight>(
		APointLight::StaticClass(), Bucket->GetActorLocation(), FRotator::ZeroRotator, Params);
	if (!Light) return;

	if (UPointLightComponent* C = Light->PointLightComponent)
	{
		C->SetMobility(EComponentMobility::Movable);
		C->SetLightColor(Color);
		C->SetIntensity(LightIntensity);
		C->SetAttenuationRadius(LightAttenuationRadius);
		C->SetCastShadows(false);
	}
	Light->SetLifeSpan(Lifetime);
}

void AAssemblyLineFeedback::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAssemblyLineDirector* D = BoundDirector.Get())
	{
		D->OnCycleCompleted.Remove(CycleCompletedHandle);
		D->OnCycleRejected.Remove(CycleRejectedHandle);
	}
	Super::EndPlay(EndPlayReason);
}
