#include "WorkerRobot.h"
#include "Bucket.h"
#include "Station.h"
#include "AssemblyLineTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AWorkerRobot::AWorkerRobot()
{
	PrimaryActorTick.bCanEverTick = true;

	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CapsuleComponent"));
	CapsuleComponent->InitCapsuleSize(40.f, 90.f);
	RootComponent = CapsuleComponent;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(RootComponent);
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
	BodyMesh->SetRelativeScale3D(FVector(0.7f, 0.7f, 1.6f));
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeadMesh"));
	HeadMesh->SetupAttachment(RootComponent);
	HeadMesh->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
	HeadMesh->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.5f));
	HeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (CubeFinder.Succeeded())   BodyMesh->SetStaticMesh(CubeFinder.Object);
	if (SphereFinder.Succeeded()) HeadMesh->SetStaticMesh(SphereFinder.Object);

	SkeletalBodyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalBodyMesh"));
	SkeletalBodyMesh->SetupAttachment(RootComponent);
	SkeletalBodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkeletalBodyMesh->SetVisibility(false);  // hidden until ApplyBodyMesh receives a mesh

	// Composite robot body — these REPLACE the legacy BodyMesh+HeadMesh visually.
	auto MakePart = [this](const TCHAR* Name, UStaticMesh* Mesh, FVector Loc, FVector Scale)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetRelativeLocation(Loc);
		C->SetRelativeScale3D(Scale);
		if (Mesh) C->SetStaticMesh(Mesh);
		return C;
	};

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* CylinderMesh = CylinderFinder.Succeeded() ? CylinderFinder.Object : nullptr;

	Torso    = MakePart(TEXT("Torso"),    CylinderMesh,                  FVector(0.f,   0.f,  -45.f), FVector(0.6f, 0.6f, 0.8f));
	HeadDome = MakePart(TEXT("HeadDome"), SphereFinder.Object,           FVector(0.f,   0.f,   25.f), FVector(0.5f, 0.5f, 0.5f));
	Eye      = MakePart(TEXT("Eye"),      SphereFinder.Object,           FVector(25.f,  0.f,   30.f), FVector(0.18f, 0.12f, 0.18f));
	LeftArm  = MakePart(TEXT("LeftArm"),  CylinderMesh,                  FVector(0.f, -45.f,  -30.f), FVector(0.18f, 0.18f, 0.7f));
	RightArm = MakePart(TEXT("RightArm"), CylinderMesh,                  FVector(0.f,  45.f,  -30.f), FVector(0.18f, 0.18f, 0.7f));
	Antenna  = MakePart(TEXT("Antenna"),  CylinderMesh,                  FVector(0.f,   0.f,   65.f), FVector(0.04f, 0.04f, 0.3f));

	// Legacy primitives become hidden fallback — kept so older tests / API stay valid.
	BodyMesh->SetVisibility(false);
	HeadMesh->SetVisibility(false);

	CarrySocket = CreateDefaultSubobject<USceneComponent>(TEXT("CarrySocket"));
	CarrySocket->SetupAttachment(RootComponent);
	CarrySocket->SetRelativeLocation(FVector(60.f, 0.f, 0.f));

	StateLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StateLabel"));
	StateLabel->SetupAttachment(RootComponent);
	StateLabel->SetRelativeLocation(FVector(0.f, 0.f, 180.f));
	StateLabel->SetHorizontalAlignment(EHTA_Center);
	StateLabel->SetVerticalAlignment(EVRTA_TextCenter);
	StateLabel->SetWorldSize(40.f);
	StateLabel->SetTextRenderColor(FColor::White);
	StateLabel->SetText(FText::FromString(TEXT("Idle")));
}

void AWorkerRobot::ApplyBodyMesh(USkeletalMesh* ResolvedMesh)
{
	if (!ResolvedMesh || !SkeletalBodyMesh) return;
	SkeletalBodyMesh->SetSkeletalMeshAsset(ResolvedMesh);
	SkeletalBodyMesh->SetVisibility(true);

	const TArray<UStaticMeshComponent*> AllPrimitives = {
		Torso, HeadDome, Eye, LeftArm, RightArm, Antenna, BodyMesh, HeadMesh
	};
	for (UStaticMeshComponent* Part : AllPrimitives)
	{
		if (Part) Part->SetVisibility(false);
	}
}

void AWorkerRobot::LoadAndApplyBodyMesh()
{
	if (BodyMeshAsset.IsNull()) return;
	ApplyBodyMesh(BodyMeshAsset.LoadSynchronous());
}

void AWorkerRobot::ApplyTint(const FLinearColor& Color)
{
	auto TintComponent = [&Color](UMeshComponent* Comp)
	{
		if (!Comp) return;
		const int32 NumMaterials = Comp->GetNumMaterials();
		for (int32 i = 0; i < NumMaterials; ++i)
		{
			if (UMaterialInstanceDynamic* MID = Comp->CreateAndSetMaterialInstanceDynamic(i))
			{
				MID->SetVectorParameterValue(TEXT("BodyTint"), Color);
			}
		}
	};

	if (SkeletalBodyMesh && SkeletalBodyMesh->IsVisible())
	{
		TintComponent(SkeletalBodyMesh);
		return;
	}

	const TArray<UStaticMeshComponent*> Composite = { Torso, HeadDome, Eye, LeftArm, RightArm, Antenna };
	for (UStaticMeshComponent* Part : Composite)
	{
		TintComponent(Part);
	}
}

void AWorkerRobot::AssignStation(AStation* Station)
{
	AssignedStation = Station;
	if (Station)
	{
		HomeLocation = Station->WorkerStandPoint
			? Station->WorkerStandPoint->GetComponentLocation()
			: Station->GetActorLocation();
		SetActorLocation(HomeLocation);
	}
}

void AWorkerRobot::BeginTask(ABucket* Bucket, USceneComponent* FromSlot, USceneComponent* ToSlot, FWorkerTaskComplete OnComplete)
{
	CurrentBucket = Bucket;
	FromSlotPtr = FromSlot;
	ToSlotPtr = ToSlot;
	TaskCompleteCb = OnComplete;
	EnterState(EWorkerState::MoveToInput);
}

void AWorkerRobot::EnterState(EWorkerState NewState)
{
	State = NewState;
	switch (State)
	{
	case EWorkerState::MoveToInput:
		if (FromSlotPtr.IsValid())
		{
			TargetLocation = FromSlotPtr->GetComponentLocation();
			TargetLocation.Z = GetActorLocation().Z;
		}
		break;
	case EWorkerState::PickUp:
		AttachBucket();
		EnterState(EWorkerState::MoveToWorkPos);
		return;
	case EWorkerState::MoveToWorkPos:
		TargetLocation = HomeLocation;
		break;
	case EWorkerState::Working:
		WorkTimer = 0.f;
		break;
	case EWorkerState::MoveToOutput:
		if (ToSlotPtr.IsValid())
		{
			TargetLocation = ToSlotPtr->GetComponentLocation();
			TargetLocation.Z = GetActorLocation().Z;
		}
		break;
	case EWorkerState::Place:
		if (ToSlotPtr.IsValid()) DetachBucketAt(ToSlotPtr.Get());
		EnterState(EWorkerState::ReturnHome);
		return;
	case EWorkerState::ReturnHome:
		TargetLocation = HomeLocation;
		break;
	case EWorkerState::Idle:
		break;
	}
	UpdateLabel();
}

void AWorkerRobot::UpdateLabel()
{
	if (!StateLabel) return;
	const TCHAR* Name = TEXT("Idle");
	switch (State)
	{
	case EWorkerState::MoveToInput:   Name = TEXT("Fetching bucket"); break;
	case EWorkerState::PickUp:        Name = TEXT("Picking up"); break;
	case EWorkerState::MoveToWorkPos: Name = TEXT("Moving to station"); break;
	case EWorkerState::Working:       Name = TEXT("Working..."); break;
	case EWorkerState::MoveToOutput:  Name = TEXT("Delivering"); break;
	case EWorkerState::Place:         Name = TEXT("Placing"); break;
	case EWorkerState::ReturnHome:    Name = TEXT("Returning"); break;
	default:                          Name = TEXT("Idle"); break;
	}
	StateLabel->SetText(FText::FromString(Name));
}

bool AWorkerRobot::MoveToward(const FVector& Target, float DeltaSeconds)
{
	const FVector Cur = GetActorLocation();
	const FVector Delta = Target - Cur;
	const float Dist = Delta.Size();
	if (Dist < 5.f)
	{
		SetActorLocation(Target);
		return true;
	}
	const FVector Step = Delta.GetSafeNormal() * FMath::Min(MoveSpeed * DeltaSeconds, Dist);
	SetActorLocation(Cur + Step);
	const FRotator Look = Delta.Rotation();
	SetActorRotation(FRotator(0.f, Look.Yaw, 0.f));
	return false;
}

void AWorkerRobot::AttachBucket()
{
	if (!CurrentBucket || !CarrySocket) return;
	CurrentBucket->AttachToComponent(CarrySocket,
		FAttachmentTransformRules::SnapToTargetIncludingScale);
	CurrentBucket->SetActorRelativeLocation(FVector::ZeroVector);
}

void AWorkerRobot::DetachBucketAt(USceneComponent* Slot)
{
	if (!CurrentBucket || !Slot) return;
	CurrentBucket->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	CurrentBucket->SetActorLocation(Slot->GetComponentLocation());
}

void AWorkerRobot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	switch (State)
	{
	case EWorkerState::MoveToInput:
		if (MoveToward(TargetLocation, DeltaSeconds))
		{
			EnterState(EWorkerState::PickUp);
		}
		break;
	case EWorkerState::MoveToWorkPos:
		if (MoveToward(TargetLocation, DeltaSeconds))
		{
			EnterState(EWorkerState::Working);
		}
		break;
	case EWorkerState::Working:
	{
		WorkTimer += DeltaSeconds;
		const float Required = (AssignedStation && AssignedStation->StationType == EStationType::Checker)
			? FMath::Max(WorkDuration, 10.0f)  // LLM call (~2s) + streaming reveal (~6s) + buffer
			: WorkDuration;

		if (WorkTimer >= Required)
		{
			if (AssignedStation && CurrentBucket)
			{
				TWeakObjectPtr<AWorkerRobot> WeakThis(this);
				ABucket* Bucket = CurrentBucket;
				AssignedStation->ProcessBucket(Bucket,
					FStationProcessComplete::CreateLambda([WeakThis](FStationProcessResult Result)
					{
						if (AWorkerRobot* Self = WeakThis.Get())
						{
							Self->LastResult = Result;
							if (Self->StateLabel)
							{
								Self->StateLabel->SetText(FText::FromString(
									FString::Printf(TEXT("%s: %s"),
										Result.bAccepted ? TEXT("PASS") : TEXT("REJECT"),
										*Result.Reason)));
							}
							Self->EnterState(EWorkerState::MoveToOutput);
						}
					}));
			}
			else
			{
				EnterState(EWorkerState::MoveToOutput);
			}
			// Only park in Idle if the completion delegate hasn't already advanced us.
			// Sync stations fire the delegate inside ProcessBucket; async ones fire it later.
			if (State == EWorkerState::Working)
			{
				State = EWorkerState::Idle;
			}
		}
		break;
	}
	case EWorkerState::MoveToOutput:
		if (MoveToward(TargetLocation, DeltaSeconds))
		{
			EnterState(EWorkerState::Place);
		}
		break;
	case EWorkerState::ReturnHome:
		if (MoveToward(TargetLocation, DeltaSeconds))
		{
			EnterState(EWorkerState::Idle);
			ABucket* Bucket = CurrentBucket;
			CurrentBucket = nullptr;
			TaskCompleteCb.ExecuteIfBound(Bucket);
		}
		break;
	default:
		break;
	}
}
