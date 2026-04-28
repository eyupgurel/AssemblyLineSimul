#include "Station.h"
#include "AgentChatSubsystem.h"
#include "Bucket.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

AStation::AStation()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// Demo direction: no station body / table — buckets appear to float so the
	// glowing gold wireframe reads cleanly against the floor. The component
	// stays in the hierarchy as a structural anchor but renders nothing.
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(RootComponent);
	MeshComponent->SetVisibility(false);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	InputSlot = CreateDefaultSubobject<USceneComponent>(TEXT("InputSlot"));
	InputSlot->SetupAttachment(RootComponent);
	InputSlot->SetRelativeLocation(FVector(0.f, -200.f, 100.f));

	OutputSlot = CreateDefaultSubobject<USceneComponent>(TEXT("OutputSlot"));
	OutputSlot->SetupAttachment(RootComponent);
	OutputSlot->SetRelativeLocation(FVector(0.f, 200.f, 100.f));

	WorkerStandPoint = CreateDefaultSubobject<USceneComponent>(TEXT("WorkerStandPoint"));
	WorkerStandPoint->SetupAttachment(RootComponent);
	WorkerStandPoint->SetRelativeLocation(FVector(-100.f, 130.f, 0.f));

	// WorkTable renders nothing (table hidden), but its relative transform still
	// positions BucketDockPoint (a child of WorkTable) at the bucket's world Z
	// — so the dock chain stays load-bearing even with no visible table.
	WorkTable = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WorkTable"));
	WorkTable->SetupAttachment(RootComponent);
	WorkTable->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WorkTable->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	WorkTable->SetRelativeScale3D(FVector(2.5f, 2.0f, 0.15f));
	WorkTable->SetVisibility(false);

	BucketDockPoint = CreateDefaultSubobject<USceneComponent>(TEXT("BucketDockPoint"));
	BucketDockPoint->SetupAttachment(WorkTable);
	BucketDockPoint->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	// Counter the WorkTable's flat scale so the dock doesn't carry it onto child attachments.
	BucketDockPoint->SetRelativeScale3D(FVector(1.f / 2.5f, 1.f / 2.0f, 1.f / 0.15f));

	ActiveLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ActiveLight"));
	ActiveLight->SetupAttachment(RootComponent);
	ActiveLight->SetRelativeLocation(FVector(0.f, 0.f, 200.f));
	ActiveLight->SetIntensity(0.f);  // off by default; SetActive(true) lights it
	ActiveLight->SetAttenuationRadius(800.f);
	ActiveLight->SetLightColor(FLinearColor(0.4f, 1.0f, 1.0f));  // cyan
}

void AStation::ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete)
{
	FStationProcessResult Result;
	Result.bAccepted = true;
	OnComplete.ExecuteIfBound(Result);
}

void AStation::SetActive(bool bActive)
{
	if (ActiveLight)
	{
		ActiveLight->SetIntensity(bActive ? 8000.f : 0.f);
	}
}

void AStation::SpeakAloud(const FString& Text)
{
	UAgentChatSubsystem* Chat = TestChatOverride;
	if (!Chat)
	{
		if (UWorld* W = GetWorld())
		{
			if (UGameInstance* GI = W->GetGameInstance())
			{
				Chat = GI->GetSubsystem<UAgentChatSubsystem>();
			}
		}
	}
	if (Chat)
	{
		Chat->SpeakResponse(Text);
	}
}
