#include "Bucket.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Crate inner extents (cm) — balls live inside, edges trace the perimeter.
	constexpr float CrateHalfX = 30.f;
	constexpr float CrateHalfY = 30.f;
	constexpr float CrateHalfZ = 20.f;

	constexpr int32 BallGridCols = 5;
	constexpr float BallSpacing  = 12.f;
	constexpr float BallScale    = 0.10f;
	constexpr float LabelSize    = 12.f;
}

ABucket::ABucket()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeFinder.Object);
		MeshComponent->SetWorldScale3D(FVector(0.6f, 0.6f, 0.4f));
	}
	MeshComponent->SetVisibility(false);  // hidden — wireframe edges replace it

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* CylinderMesh = CylinderFinder.Succeeded() ? CylinderFinder.Object : nullptr;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CachedSphereMesh = SphereFinder.Succeeded() ? SphereFinder.Object : nullptr;

	// 12 crate edges: 4 vertical posts + 4 top + 4 bottom horizontals.
	// Cylinders are 100 cm tall along Z by default; we scale + rotate per edge.
	struct FEdgeSpec { FVector Loc; FRotator Rot; FVector Scale; };
	const float ThinR = 0.05f;
	const FEdgeSpec Specs[12] = {
		// Verticals (4): X axis aligned with Z, scale Z to height
		{ FVector( CrateHalfX,  CrateHalfY, 0.f), FRotator::ZeroRotator, FVector(ThinR, ThinR, CrateHalfZ * 2.f / 100.f) },
		{ FVector( CrateHalfX, -CrateHalfY, 0.f), FRotator::ZeroRotator, FVector(ThinR, ThinR, CrateHalfZ * 2.f / 100.f) },
		{ FVector(-CrateHalfX,  CrateHalfY, 0.f), FRotator::ZeroRotator, FVector(ThinR, ThinR, CrateHalfZ * 2.f / 100.f) },
		{ FVector(-CrateHalfX, -CrateHalfY, 0.f), FRotator::ZeroRotator, FVector(ThinR, ThinR, CrateHalfZ * 2.f / 100.f) },
		// Top horizontals (4): rotate cylinder to lie along X or Y
		{ FVector(0.f,  CrateHalfY,  CrateHalfZ), FRotator(90.f, 0.f, 0.f), FVector(ThinR, ThinR, CrateHalfX * 2.f / 100.f) },
		{ FVector(0.f, -CrateHalfY,  CrateHalfZ), FRotator(90.f, 0.f, 0.f), FVector(ThinR, ThinR, CrateHalfX * 2.f / 100.f) },
		{ FVector( CrateHalfX, 0.f,  CrateHalfZ), FRotator(90.f, 90.f, 0.f), FVector(ThinR, ThinR, CrateHalfY * 2.f / 100.f) },
		{ FVector(-CrateHalfX, 0.f,  CrateHalfZ), FRotator(90.f, 90.f, 0.f), FVector(ThinR, ThinR, CrateHalfY * 2.f / 100.f) },
		// Bottom horizontals (4)
		{ FVector(0.f,  CrateHalfY, -CrateHalfZ), FRotator(90.f, 0.f, 0.f), FVector(ThinR, ThinR, CrateHalfX * 2.f / 100.f) },
		{ FVector(0.f, -CrateHalfY, -CrateHalfZ), FRotator(90.f, 0.f, 0.f), FVector(ThinR, ThinR, CrateHalfX * 2.f / 100.f) },
		{ FVector( CrateHalfX, 0.f, -CrateHalfZ), FRotator(90.f, 90.f, 0.f), FVector(ThinR, ThinR, CrateHalfY * 2.f / 100.f) },
		{ FVector(-CrateHalfX, 0.f, -CrateHalfZ), FRotator(90.f, 90.f, 0.f), FVector(ThinR, ThinR, CrateHalfY * 2.f / 100.f) },
	};

	for (int32 i = 0; i < 12; ++i)
	{
		const FName EdgeName = *FString::Printf(TEXT("CrateEdge_%d"), i);
		UStaticMeshComponent* Edge = CreateDefaultSubobject<UStaticMeshComponent>(EdgeName);
		Edge->SetupAttachment(RootComponent);
		Edge->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Edge->SetRelativeLocation(Specs[i].Loc);
		Edge->SetRelativeRotation(Specs[i].Rot);
		Edge->SetRelativeScale3D(Specs[i].Scale);
		if (CylinderMesh) Edge->SetStaticMesh(CylinderMesh);
		CrateEdges.Add(Edge);
	}
}

void ABucket::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Apply cyan tint to crate edges via dynamic material instances.
	const FLinearColor EdgeColor(0.4f, 0.95f, 1.0f, 1.f);
	for (UStaticMeshComponent* Edge : CrateEdges)
	{
		if (!Edge) continue;
		if (UMaterialInstanceDynamic* MID = Edge->CreateAndSetMaterialInstanceDynamic(0))
		{
			MID->SetVectorParameterValue(TEXT("BodyTint"), EdgeColor);
		}
	}
}

void ABucket::Tick(float /*DeltaSeconds*/)
{
	// Billboard each ball's text label to face the player camera.
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!Cam) return;
	const FVector CamLoc = Cam->GetCameraLocation();
	for (UTextRenderComponent* Label : NumberBallLabels)
	{
		if (!Label) continue;
		FRotator LookAt = (CamLoc - Label->GetComponentLocation()).Rotation();
		LookAt.Pitch = 0.f;
		LookAt.Roll = 0.f;
		LookAt.Yaw += 180.f;
		Label->SetWorldRotation(LookAt);
	}
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

void ABucket::RefreshContents()
{
	for (UStaticMeshComponent* Ball : NumberBalls)
	{
		if (Ball) Ball->DestroyComponent();
	}
	for (UTextRenderComponent* Label : NumberBallLabels)
	{
		if (Label) Label->DestroyComponent();
	}
	NumberBalls.Reset();
	NumberBallLabels.Reset();

	const int32 N = Contents.Num();
	const int32 Rows = FMath::DivideAndRoundUp(N, BallGridCols);
	const float OffsetX = -0.5f * (BallGridCols - 1) * BallSpacing;
	const float OffsetY = -0.5f * (Rows - 1) * BallSpacing;

	for (int32 i = 0; i < N; ++i)
	{
		const int32 Col = i % BallGridCols;
		const int32 Row = i / BallGridCols;
		const FVector LocalLoc(OffsetX + Col * BallSpacing, OffsetY + Row * BallSpacing, 0.f);

		const FName BallName = *FString::Printf(TEXT("NumberBall_%d"), i);
		UStaticMeshComponent* Ball = NewObject<UStaticMeshComponent>(this, BallName);
		Ball->SetupAttachment(RootComponent);
		Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ball->SetRelativeLocation(LocalLoc);
		Ball->SetRelativeScale3D(FVector(BallScale));
		if (CachedSphereMesh) Ball->SetStaticMesh(CachedSphereMesh);
		Ball->RegisterComponent();
		NumberBalls.Add(Ball);

		const FName LabelName = *FString::Printf(TEXT("NumberBallLabel_%d"), i);
		UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this, LabelName);
		Label->SetupAttachment(Ball);
		// World-size scales with parent — undo the small ball scale so text is readable.
		Label->SetRelativeScale3D(FVector(1.f / BallScale));
		Label->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetVerticalAlignment(EVRTA_TextCenter);
		Label->SetWorldSize(LabelSize);
		Label->SetTextRenderColor(FColor::Yellow);
		Label->SetText(FText::FromString(FString::FromInt(Contents[i])));
		Label->RegisterComponent();
		NumberBallLabels.Add(Label);
	}
}
