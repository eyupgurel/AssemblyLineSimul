#include "Bucket.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "UObject/ConstructorHelpers.h"

ABucket::ABucket()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeFinder.Object);
		MeshComponent->SetWorldScale3D(FVector(0.6f, 0.6f, 0.4f));
	}

	ContentsLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ContentsLabel"));
	ContentsLabel->SetupAttachment(RootComponent);
	ContentsLabel->SetRelativeLocation(FVector(0.f, 0.f, 80.f));
	ContentsLabel->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
	ContentsLabel->SetHorizontalAlignment(EHTA_Center);
	ContentsLabel->SetVerticalAlignment(EVRTA_TextCenter);
	ContentsLabel->SetWorldSize(20.f);
	ContentsLabel->SetTextRenderColor(FColor::Yellow);
	ContentsLabel->SetText(FText::FromString(TEXT("[]")));
}

void ABucket::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshLabel();
}

FString ABucket::GetContentsString() const
{
	if (Contents.Num() == 0)
	{
		return TEXT("[]");
	}

	FString Out = TEXT("[");
	for (int32 i = 0; i < Contents.Num(); ++i)
	{
		Out += FString::FromInt(Contents[i]);
		if (i < Contents.Num() - 1)
		{
			Out += TEXT(", ");
		}
	}
	Out += TEXT("]");
	return Out;
}

void ABucket::RefreshLabel()
{
	if (ContentsLabel)
	{
		ContentsLabel->SetText(FText::FromString(GetContentsString()));
	}
}
