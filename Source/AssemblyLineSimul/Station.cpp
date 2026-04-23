#include "Station.h"
#include "Bucket.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "UObject/ConstructorHelpers.h"

AStation::AStation()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeFinder.Object);
		MeshComponent->SetWorldScale3D(FVector(2.0f, 3.0f, 1.0f));
	}

	InputSlot = CreateDefaultSubobject<USceneComponent>(TEXT("InputSlot"));
	InputSlot->SetupAttachment(RootComponent);
	InputSlot->SetRelativeLocation(FVector(0.f, -200.f, 100.f));

	OutputSlot = CreateDefaultSubobject<USceneComponent>(TEXT("OutputSlot"));
	OutputSlot->SetupAttachment(RootComponent);
	OutputSlot->SetRelativeLocation(FVector(0.f, 200.f, 100.f));

	WorkerStandPoint = CreateDefaultSubobject<USceneComponent>(TEXT("WorkerStandPoint"));
	WorkerStandPoint->SetupAttachment(RootComponent);
	WorkerStandPoint->SetRelativeLocation(FVector(-250.f, 0.f, 0.f));

	NameLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameLabel"));
	NameLabel->SetupAttachment(RootComponent);
	NameLabel->SetRelativeLocation(FVector(0.f, 0.f, 200.f));
	NameLabel->SetHorizontalAlignment(EHTA_Center);
	NameLabel->SetVerticalAlignment(EVRTA_TextCenter);
	NameLabel->SetWorldSize(40.f);
	NameLabel->SetTextRenderColor(FColor::Cyan);
}

void AStation::BeginPlay()
{
	Super::BeginPlay();
	if (NameLabel)
	{
		NameLabel->SetText(FText::FromString(DisplayName));
	}
}

void AStation::ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete)
{
	FStationProcessResult Result;
	Result.bAccepted = true;
	OnComplete.ExecuteIfBound(Result);
}
