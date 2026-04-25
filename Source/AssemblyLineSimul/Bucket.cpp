#include "Bucket.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
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
	constexpr float LabelSize    = 35.f;
	constexpr float LabelOffsetZ = 150.f;  // in ball-local space; world Z = LabelOffsetZ * BallScale

	constexpr int32 BillboardTextureSize = 512;

	// Pool-ball palette indexed by Number % PaletteSize. First 8 mirror real billiard colors.
	static const FLinearColor BallPalette[] = {
		FLinearColor(1.0f,  0.85f, 0.0f,  1.f),  // 0 -> yellow
		FLinearColor(0.0f,  0.3f,  0.85f, 1.f),  // 1 -> blue
		FLinearColor(0.85f, 0.1f,  0.1f,  1.f),  // 2 -> red
		FLinearColor(0.5f,  0.1f,  0.55f, 1.f),  // 3 -> purple
		FLinearColor(1.0f,  0.5f,  0.0f,  1.f),  // 4 -> orange
		FLinearColor(0.0f,  0.5f,  0.2f,  1.f),  // 5 -> green
		FLinearColor(0.5f,  0.05f, 0.1f,  1.f),  // 6 -> maroon
		FLinearColor(0.05f, 0.05f, 0.05f, 1.f),  // 7 -> black
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
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeFinder.Object);
	}
	MeshComponent->SetVisibility(false);  // hidden — wireframe edges replace it
	// Root scale stays at (1,1,1) so crate edges and balls render in raw cm units.

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
	const bool bWasHidden = IsHidden();

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
					// dark balls, dark text on light ones. Outline always uses the inverse so
					// the digit pops regardless of background.
					const FLinearColor BallColor = PickBallColor(Contents[i]);
					const float Luminance = 0.299f * BallColor.R + 0.587f * BallColor.G + 0.114f * BallColor.B;
					const bool bLightBall = Luminance > 0.5f;
					const FLinearColor TextColor    = bLightBall ? FLinearColor::Black : FLinearColor::White;
					const FLinearColor OutlineColor = bLightBall ? FLinearColor::White : FLinearColor::Black;

					// Draw the number at 5 positions across the texture so that at least one
					// copy is visible from any viewing angle on the sphere.
					struct FNumberStamp { FVector2D Pos; FVector2D Scale; };
					const FNumberStamp Stamps[] = {
						{ FVector2D(CenterX,              CenterY),              FVector2D(10.0f, 10.0f) },
						{ FVector2D(CenterX,              CenterY - EdgeOffset), FVector2D(5.0f,  5.0f)  },
						{ FVector2D(CenterX,              CenterY + EdgeOffset), FVector2D(5.0f,  5.0f)  },
						{ FVector2D(CenterX - EdgeOffset, CenterY),              FVector2D(5.0f,  5.0f)  },
						{ FVector2D(CenterX + EdgeOffset, CenterY),              FVector2D(5.0f,  5.0f)  },
					};
					for (const FNumberStamp& S : Stamps)
					{
						Canvas->K2_DrawText(CachedNumberFont, Text, S.Pos, S.Scale,
							TextColor, 0.f,
							OutlineColor, FVector2D(0.f, 0.f),
							true, true, /*bOutlined=*/true, OutlineColor);
					}
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

		// Skip the floating-number fallback when the billiard material is in place — the
		// number is already painted onto the ball, no need for a duplicate label.
		if (BilliardBallMaterial.IsNull())
		{
			const FName LabelName = *FString::Printf(TEXT("NumberBallLabel_%d"), i);
			UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this, LabelName);
			Label->SetupAttachment(Ball);
			// Undo the ball's local scale so text renders at world-cm units.
			Label->SetRelativeScale3D(FVector(1.f / BallScale));
			Label->SetRelativeLocation(FVector(0.f, 0.f, LabelOffsetZ));
			Label->SetHorizontalAlignment(EHTA_Center);
			Label->SetVerticalAlignment(EVRTA_TextCenter);
			Label->SetWorldSize(LabelSize);
			Label->SetTextRenderColor(FColor::Yellow);
			Label->SetText(FText::FromString(FString::FromInt(Contents[i])));
			Label->RegisterComponent();
			NumberBallLabels.Add(Label);
		}
	}
}
