#include "PayloadVisualizer.h"
#include "Payload.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Crate inner extents (cm) — balls live inside, edges trace the perimeter.
	constexpr float CrateHalfX = 90.f;
	constexpr float CrateHalfY = 70.f;
	constexpr float CrateHalfZ = 30.f;
	constexpr float EdgeThicknessScale = 0.10f;

	constexpr int32 BallGridCols = 4;
	constexpr float BallSpacing  = 45.f;
	constexpr float BallScale    = 0.30f;

	constexpr int32 BillboardTextureSize = 512;

	// Dark-only palette so white digits stay legible. No yellow / orange / pastels —
	// every entry's luminance stays well below 0.5.
	static const FLinearColor BallPalette[] = {
		FLinearColor(0.05f, 0.05f, 0.05f, 1.f),
		FLinearColor(0.05f, 0.15f, 0.45f, 1.f),
		FLinearColor(0.45f, 0.05f, 0.05f, 1.f),
		FLinearColor(0.30f, 0.05f, 0.40f, 1.f),
		FLinearColor(0.05f, 0.30f, 0.10f, 1.f),
		FLinearColor(0.40f, 0.05f, 0.20f, 1.f),
		FLinearColor(0.10f, 0.25f, 0.35f, 1.f),
		FLinearColor(0.30f, 0.15f, 0.05f, 1.f),
	};

	FLinearColor PickBallColor(int32 Number)
	{
		const int32 Idx = ((Number % UE_ARRAY_COUNT(BallPalette)) + UE_ARRAY_COUNT(BallPalette))
			% UE_ARRAY_COUNT(BallPalette);
		return BallPalette[Idx];
	}
}

// --- UPayloadVisualizer ----------------------------------------------------

void UPayloadVisualizer::BindPayload(UPayload* InPayload)
{
	if (BoundPayload == InPayload) return;
	BoundPayload = InPayload;
	if (BoundPayload)
	{
		BoundPayload->OnChanged.AddUObject(this, &UPayloadVisualizer::OnBoundPayloadChanged);
		Rebuild();  // initial render
	}
}

void UPayloadVisualizer::OnBoundPayloadChanged()
{
	Rebuild();
}

// --- UBilliardBallVisualizer -----------------------------------------------

UBilliardBallVisualizer::UBilliardBallVisualizer()
{
	// SceneComponents don't tick by default; we don't need it.
	PrimaryComponentTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CachedSphereMesh = SphereFinder.Succeeded() ? SphereFinder.Object : nullptr;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	CachedCylinderMesh = CylinderFinder.Succeeded() ? CylinderFinder.Object : nullptr;

	static ConstructorHelpers::FObjectFinder<UFont> FontFinder(
		TEXT("/Engine/EngineFonts/Roboto.Roboto"));
	CachedNumberFont = FontFinder.Succeeded() ? FontFinder.Object : nullptr;

	// Default to the project's M_BilliardBall master material so the visualizer
	// renders numbered/colored balls without requiring a Blueprint subclass to
	// wire BilliardBallMaterial. Pre-Story-38 BP_BilliardBucket set this; with
	// BP_BilliardBucket gone, the C++ default is what guarantees byte-identical
	// visuals for the typical mission. Designers can still override via a BP.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BallMatFinder(
		TEXT("/Game/M_BilliardBall.M_BilliardBall"));
	if (BallMatFinder.Succeeded())
	{
		BilliardBallMaterial = BallMatFinder.Object;
	}
}

void UBilliardBallVisualizer::OnRegister()
{
	Super::OnRegister();
	EnsureCrateBuilt();
}

void UBilliardBallVisualizer::EnsureCrateBuilt()
{
	if (bCrateBuilt) return;

	UStaticMesh* CylinderMesh = CachedCylinderMesh;

	UMaterialInterface* Emissive = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));

	struct FEdgeSpec { FVector Loc; FRotator Rot; FVector Scale; };
	const float T = EdgeThicknessScale;
	const FEdgeSpec Specs[12] = {
		{ FVector( CrateHalfX,  CrateHalfY, 0.f), FRotator::ZeroRotator, FVector(T, T, CrateHalfZ * 2.f / 100.f) },
		{ FVector( CrateHalfX, -CrateHalfY, 0.f), FRotator::ZeroRotator, FVector(T, T, CrateHalfZ * 2.f / 100.f) },
		{ FVector(-CrateHalfX,  CrateHalfY, 0.f), FRotator::ZeroRotator, FVector(T, T, CrateHalfZ * 2.f / 100.f) },
		{ FVector(-CrateHalfX, -CrateHalfY, 0.f), FRotator::ZeroRotator, FVector(T, T, CrateHalfZ * 2.f / 100.f) },
		{ FVector(0.f,  CrateHalfY,  CrateHalfZ), FRotator(90.f, 0.f, 0.f), FVector(T, T, CrateHalfX * 2.f / 100.f) },
		{ FVector(0.f, -CrateHalfY,  CrateHalfZ), FRotator(90.f, 0.f, 0.f), FVector(T, T, CrateHalfX * 2.f / 100.f) },
		{ FVector( CrateHalfX, 0.f,  CrateHalfZ), FRotator(90.f, 90.f, 0.f), FVector(T, T, CrateHalfY * 2.f / 100.f) },
		{ FVector(-CrateHalfX, 0.f,  CrateHalfZ), FRotator(90.f, 90.f, 0.f), FVector(T, T, CrateHalfY * 2.f / 100.f) },
		{ FVector(0.f,  CrateHalfY, -CrateHalfZ), FRotator(90.f, 0.f, 0.f), FVector(T, T, CrateHalfX * 2.f / 100.f) },
		{ FVector(0.f, -CrateHalfY, -CrateHalfZ), FRotator(90.f, 0.f, 0.f), FVector(T, T, CrateHalfX * 2.f / 100.f) },
		{ FVector( CrateHalfX, 0.f, -CrateHalfZ), FRotator(90.f, 90.f, 0.f), FVector(T, T, CrateHalfY * 2.f / 100.f) },
		{ FVector(-CrateHalfX, 0.f, -CrateHalfZ), FRotator(90.f, 90.f, 0.f), FVector(T, T, CrateHalfY * 2.f / 100.f) },
	};

	for (int32 i = 0; i < 12; ++i)
	{
		const FName EdgeName = *FString::Printf(TEXT("CrateEdge_%d"), i);
		UStaticMeshComponent* Edge = NewObject<UStaticMeshComponent>(this, EdgeName);
		Edge->SetupAttachment(this);
		Edge->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Edge->SetRelativeLocation(Specs[i].Loc);
		Edge->SetRelativeRotation(Specs[i].Rot);
		Edge->SetRelativeScale3D(Specs[i].Scale);
		if (CylinderMesh) Edge->SetStaticMesh(CylinderMesh);
		Edge->RegisterComponent();
		if (Emissive)
		{
			if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Emissive, this))
			{
				MID->SetVectorParameterValue(TEXT("Color"), GlassTint);
				Edge->SetMaterial(0, MID);
			}
		}
		CrateEdges.Add(Edge);
	}

	// Hide owning actor at start — Rebuild un-hides when items appear.
	if (AActor* Owner = GetOwner())
	{
		Owner->SetActorHiddenInGame(true);
	}

	bCrateBuilt = true;
}

void UBilliardBallVisualizer::Rebuild()
{
	UIntegerArrayPayload* P = Cast<UIntegerArrayPayload>(BoundPayload);
	if (!P)
	{
		// Wrong payload type for this visualizer — leave the crate empty
		// and hidden. Stations should never bind a non-integer payload to
		// this visualizer; if they do, the contract violation is on them.
		return;
	}

	AActor* Owner = GetOwner();
	const bool bWasHidden = Owner ? Owner->IsHidden() : true;

	for (UStaticMeshComponent* Ball : NumberBalls)
	{
		if (Ball) Ball->DestroyComponent();
	}
	NumberBalls.Reset();

	const int32 N = P->Items.Num();
	if (Owner)
	{
		Owner->SetActorHiddenInGame(N == 0);
	}

	if (bWasHidden && N > 0)
	{
		OnVisualizationRevealed.Broadcast();
	}

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
		Ball->SetupAttachment(this);
		Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ball->SetRelativeLocation(LocalLoc);
		Ball->SetRelativeRotation(BallRelativeRotation);
		Ball->SetRelativeScale3D(FVector(BallScale));
		if (CachedSphereMesh) Ball->SetStaticMesh(CachedSphereMesh);
		Ball->RegisterComponent();
		NumberBalls.Add(Ball);

		if (UMaterialInterface* Master = BilliardBallMaterial.LoadSynchronous())
		{
			UCanvasRenderTarget2D* RT = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
				this, UCanvasRenderTarget2D::StaticClass(),
				BillboardTextureSize, BillboardTextureSize);
			if (RT && CachedNumberFont)
			{
				UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, FLinearColor(0.f, 0.f, 0.f, 0.f));
				UCanvas* Canvas = nullptr;
				FVector2D Size;
				FDrawToRenderTargetContext Ctx;
				UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RT, Canvas, Size, Ctx);
				if (Canvas)
				{
					const FString Text = FString::FromInt(P->Items[i]);
					const float CenterX = BillboardTextureSize * 0.5f;
					const float CenterY = BillboardTextureSize * 0.5f;

					const FLinearColor BallColor = PickBallColor(P->Items[i]);
					const float Luminance = 0.299f * BallColor.R + 0.587f * BallColor.G + 0.114f * BallColor.B;
					const bool bLightBall = Luminance > 0.5f;
					const FLinearColor TextColor = bLightBall ? FLinearColor::Black : FLinearColor::White;

					Canvas->K2_DrawText(CachedNumberFont, Text,
						FVector2D(CenterX, CenterY),
						FVector2D(10.f, 10.f),
						TextColor, 0.f,
						FLinearColor::Transparent, FVector2D(0.f, 0.f),
						true, true, /*bOutlined=*/false, FLinearColor::Transparent);
				}
				UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Ctx);
			}
			if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Master, this))
			{
				if (RT) MID->SetTextureParameterValue(TEXT("NumberTexture"), RT);
				MID->SetVectorParameterValue(TEXT("BaseColor"), PickBallColor(P->Items[i]));
				Ball->SetMaterial(0, MID);
			}
		}
	}
}

void UBilliardBallVisualizer::HighlightItemsAtIndices(const TArray<int32>& Indices)
{
	if (Indices.Num() == 0) return;
	UMaterialInterface* Emissive = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
	if (!Emissive) return;
	for (int32 i : Indices)
	{
		if (!NumberBalls.IsValidIndex(i)) continue;
		UStaticMeshComponent* Ball = NumberBalls[i];
		if (!Ball) continue;
		if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Emissive, this))
		{
			MID->SetVectorParameterValue(TEXT("Color"), GlassTint);
			Ball->SetMaterial(0, MID);
		}
	}
}

void UBilliardBallVisualizer::ClearHighlight()
{
	// Trigger a full Rebuild so every ball gets its normal billiard
	// material back. Cheap given typical bucket sizes.
	Rebuild();
}
