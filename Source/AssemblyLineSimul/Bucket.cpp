#include "Bucket.h"
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
	constexpr float EdgeThicknessScale = 0.10f;  // chunky enough to read from the high overhead

	constexpr int32 BallGridCols = 4;
	constexpr float BallSpacing  = 45.f;
	constexpr float BallScale    = 0.30f;

	constexpr int32 BillboardTextureSize = 512;

	// Dark-only palette so white digits stay legible. No yellow / orange / pastels —
	// every entry's luminance stays well below 0.5.
	static const FLinearColor BallPalette[] = {
		FLinearColor(0.05f, 0.05f, 0.05f, 1.f),  // 0 -> near-black
		FLinearColor(0.05f, 0.15f, 0.45f, 1.f),  // 1 -> deep blue
		FLinearColor(0.45f, 0.05f, 0.05f, 1.f),  // 2 -> dark red
		FLinearColor(0.30f, 0.05f, 0.40f, 1.f),  // 3 -> dark purple
		FLinearColor(0.05f, 0.30f, 0.10f, 1.f),  // 4 -> forest green
		FLinearColor(0.40f, 0.05f, 0.20f, 1.f),  // 5 -> wine
		FLinearColor(0.10f, 0.25f, 0.35f, 1.f),  // 6 -> teal-slate
		FLinearColor(0.30f, 0.15f, 0.05f, 1.f),  // 7 -> dark brown
	};

	FLinearColor PickBallColor(int32 Number)
	{
		const int32 Idx = ((Number % UE_ARRAY_COUNT(BallPalette)) + UE_ARRAY_COUNT(BallPalette))
			% UE_ARRAY_COUNT(BallPalette);
		return BallPalette[Idx];
	}
}

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
	}
	// Engine cube is 100cm; scale to the crate's inner extents (× 2 because half-extents).
	MeshComponent->SetRelativeScale3D(FVector(
		CrateHalfX * 2.f / 100.f,
		CrateHalfY * 2.f / 100.f,
		CrateHalfZ * 2.f / 100.f));
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Hidden by default — OnConstruction shows the cube with EmissiveMeshMaterial
	// to render the bucket as a solid glowing-gold block.
	MeshComponent->SetVisibility(false);

	// Hide the entire actor at spawn — RefreshContents un-hides it when Contents is non-empty.
	SetActorHiddenInGame(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* CylinderMesh = CylinderFinder.Succeeded() ? CylinderFinder.Object : nullptr;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CachedSphereMesh = SphereFinder.Succeeded() ? SphereFinder.Object : nullptr;

	static ConstructorHelpers::FObjectFinder<UFont> FontFinder(
		TEXT("/Engine/EngineFonts/Roboto.Roboto"));
	CachedNumberFont = FontFinder.Succeeded() ? FontFinder.Object : nullptr;

	// 12 crate edges: 4 vertical posts + 4 top + 4 bottom horizontals.
	// Cylinders are 100 cm tall along Z by default; we scale + rotate per edge.
	struct FEdgeSpec { FVector Loc; FRotator Rot; FVector Scale; };
	const float T = EdgeThicknessScale;
	const FEdgeSpec Specs[12] = {
		// Verticals (4)
		{ FVector( CrateHalfX,  CrateHalfY, 0.f), FRotator::ZeroRotator, FVector(T, T, CrateHalfZ * 2.f / 100.f) },
		{ FVector( CrateHalfX, -CrateHalfY, 0.f), FRotator::ZeroRotator, FVector(T, T, CrateHalfZ * 2.f / 100.f) },
		{ FVector(-CrateHalfX,  CrateHalfY, 0.f), FRotator::ZeroRotator, FVector(T, T, CrateHalfZ * 2.f / 100.f) },
		{ FVector(-CrateHalfX, -CrateHalfY, 0.f), FRotator::ZeroRotator, FVector(T, T, CrateHalfZ * 2.f / 100.f) },
		// Top horizontals (4)
		{ FVector(0.f,  CrateHalfY,  CrateHalfZ), FRotator(90.f, 0.f, 0.f), FVector(T, T, CrateHalfX * 2.f / 100.f) },
		{ FVector(0.f, -CrateHalfY,  CrateHalfZ), FRotator(90.f, 0.f, 0.f), FVector(T, T, CrateHalfX * 2.f / 100.f) },
		{ FVector( CrateHalfX, 0.f,  CrateHalfZ), FRotator(90.f, 90.f, 0.f), FVector(T, T, CrateHalfY * 2.f / 100.f) },
		{ FVector(-CrateHalfX, 0.f,  CrateHalfZ), FRotator(90.f, 90.f, 0.f), FVector(T, T, CrateHalfY * 2.f / 100.f) },
		// Bottom horizontals (4)
		{ FVector(0.f,  CrateHalfY, -CrateHalfZ), FRotator(90.f, 0.f, 0.f), FVector(T, T, CrateHalfX * 2.f / 100.f) },
		{ FVector(0.f, -CrateHalfY, -CrateHalfZ), FRotator(90.f, 0.f, 0.f), FVector(T, T, CrateHalfX * 2.f / 100.f) },
		{ FVector( CrateHalfX, 0.f, -CrateHalfZ), FRotator(90.f, 90.f, 0.f), FVector(T, T, CrateHalfY * 2.f / 100.f) },
		{ FVector(-CrateHalfX, 0.f, -CrateHalfZ), FRotator(90.f, 90.f, 0.f), FVector(T, T, CrateHalfY * 2.f / 100.f) },
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

	// Force the cube to crate dims here (not just in the constructor) so an existing
	// Blueprint subclass with a serialised scale can't shrink the glass walls.
	MeshComponent->SetRelativeScale3D(FVector(
		CrateHalfX * 2.f / 100.f,
		CrateHalfY * 2.f / 100.f,
		CrateHalfZ * 2.f / 100.f));

	// Demo direction: glowing-gold wireframe bucket — no inner cube, only the
	// 12 crate edges glow. EmissiveMeshMaterial's Color parameter drives the
	// emissive output directly so RGB > 1 in GlassTint triggers HDR bloom.
	MeshComponent->SetVisibility(false);

	UMaterialInterface* Emissive = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
	for (UStaticMeshComponent* Edge : CrateEdges)
	{
		if (!Edge) continue;
		Edge->SetVisibility(true);
		if (Emissive)
		{
			if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Emissive, this))
			{
				MID->SetVectorParameterValue(TEXT("Color"), GlassTint);
				Edge->SetMaterial(0, MID);
			}
		}
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
	const bool bWasHidden = IsHidden();

	for (UStaticMeshComponent* Ball : NumberBalls)
	{
		if (Ball) Ball->DestroyComponent();
	}
	NumberBalls.Reset();

	const int32 N = Contents.Num();
	// Hide the bucket entirely when empty so the audience never sees an unfilled crate.
	SetActorHiddenInGame(N == 0);

	// Notify observers when the crate transitions from empty/hidden to filled/visible.
	if (bWasHidden && N > 0)
	{
		OnContentsRevealed.Broadcast();
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
		Ball->SetupAttachment(RootComponent);
		Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ball->SetRelativeLocation(LocalLoc);
		Ball->SetRelativeRotation(BallRelativeRotation);
		Ball->SetRelativeScale3D(FVector(BallScale));
		if (CachedSphereMesh) Ball->SetStaticMesh(CachedSphereMesh);
		Ball->RegisterComponent();
		NumberBalls.Add(Ball);

		// Billiard finish — render the number into a runtime texture and apply it via a MID
		// from the configured master material (expects "NumberTexture" + "BaseColor" parameters).
		if (UMaterialInterface* Master = BilliardBallMaterial.LoadSynchronous())
		{
			UCanvasRenderTarget2D* RT = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
				this, UCanvasRenderTarget2D::StaticClass(),
				BillboardTextureSize, BillboardTextureSize);
			if (RT && CachedNumberFont)
			{
				// Clear to transparent so only the rendered text contributes alpha.
				UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, FLinearColor(0.f, 0.f, 0.f, 0.f));
				UCanvas* Canvas = nullptr;
				FVector2D Size;
				FDrawToRenderTargetContext Ctx;
				UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RT, Canvas, Size, Ctx);
				if (Canvas)
				{
					const FString Text = FString::FromInt(Contents[i]);
					const float CenterX = BillboardTextureSize * 0.5f;
					const float CenterY = BillboardTextureSize * 0.5f;
					const float EdgeOffset = BillboardTextureSize * 0.30f;

					// Pick a text color that contrasts with the ball's base color: light text on
					// dark balls, dark text on light ones. NO outline — the outlined halo was
					// dominating on bright balls and washing the digit out.
					const FLinearColor BallColor = PickBallColor(Contents[i]);
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
				MID->SetVectorParameterValue(TEXT("BaseColor"), PickBallColor(Contents[i]));
				Ball->SetMaterial(0, MID);
			}
		}

	}
}
