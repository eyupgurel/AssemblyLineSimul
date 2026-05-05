#include "WorkerRobot.h"
#include "PayloadCarrier.h"
#include "Station.h"
#include "AssemblyLineTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AWorkerRobot::AWorkerRobot()
{
	PrimaryActorTick.bCanEverTick = true;

	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CapsuleComponent"));
	CapsuleComponent->InitCapsuleSize(40.f, 90.f);
	RootComponent = CapsuleComponent;

	// Mannequin reads at human scale against the stations and bucket — capsule,
	// skeletal mesh, and composite parts all scale uniformly via the actor scale.
	SetActorScale3D(FVector(1.5f));

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
	// Drop the mesh so its feet land at the capsule's bottom (capsule half-height
	// is 90cm). UE5 mannequin's pivot is at the feet, so without this offset Manny
	// floats with feet at the capsule center.
	SkeletalBodyMesh->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
	// UE5 mannequin's forward axis is -Y; rotate so the visible forward matches
	// the worker's +X movement direction.
	SkeletalBodyMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	SkeletalBodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkeletalBodyMesh->SetVisibility(false);  // hidden until ApplyBodyMesh receives a mesh

	// Cache idle and walk animations from the engine mannequin pack.
	// EnterState calls RefreshAnimationForState to swap between them.
	static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleFinder(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> WalkFinder(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd"));
	if (IdleFinder.Succeeded()) IdleAnimation = IdleFinder.Object;
	if (WalkFinder.Succeeded()) WalkAnimation = WalkFinder.Object;
	SkeletalBodyMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	RefreshAnimationForState();

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
	CarrySocket->SetRelativeLocation(FVector(130.f, 0.f, 0.f));

	// Story 19 — green active-speaker glow on the worker. Off by default;
	// HandleActiveAgentChanged in the GameMode toggles it via SetActive.
	ActiveLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ActiveLight"));
	ActiveLight->SetupAttachment(RootComponent);
	ActiveLight->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
	ActiveLight->SetIntensity(0.f);
	ActiveLight->SetAttenuationRadius(600.f);
	ActiveLight->SetLightColor(FLinearColor(0.10f, 1.0f, 0.20f));
}

void AWorkerRobot::SetActive(bool bActive)
{
	if (!ActiveLight) return;
	ActiveLight->SetIntensity(bActive ? 8000.f : 0.f);
}

void AWorkerRobot::ApplyBodyMesh(USkeletalMesh* ResolvedMesh)
{
	if (!ResolvedMesh || !SkeletalBodyMesh) return;
	SkeletalBodyMesh->SetSkeletalMeshAsset(ResolvedMesh);
	SkeletalBodyMesh->SetVisibility(true);
	// Constructor's RefreshAnimationForState ran before any mesh existed (no
	// SingleNodeInstance to receive the call); re-apply now that the mesh is in.
	RefreshAnimationForState();

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

void AWorkerRobot::BeginTask(APayloadCarrier* Bucket, USceneComponent* FromSlot, USceneComponent* ToSlot, FWorkerTaskComplete OnComplete)
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
	RefreshAnimationForState();
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
		if (AssignedStation) OnPickedUp.Broadcast(AssignedStation->NodeRef);
		EnterState(EWorkerState::MoveToWorkPos);
		return;
	case EWorkerState::MoveToWorkPos:
		TargetLocation = HomeLocation;
		break;
	case EWorkerState::Working:
		WorkTimer = 0.f;
		// Move bucket from carry socket to the station's worktable so the worker is visually
		// outside the bucket while processing.
		if (CurrentBucket && AssignedStation && AssignedStation->BucketDockPoint)
		{
			CurrentBucket->AttachToComponent(AssignedStation->BucketDockPoint,
				FAttachmentTransformRules::SnapToTargetIncludingScale);
			CurrentBucket->SetActorRelativeLocation(FVector::ZeroVector);
		}
		if (AssignedStation) OnStartedWorking.Broadcast(AssignedStation->NodeRef);
		break;
	case EWorkerState::MoveToOutput:
		// Worker picks the bucket back up from the worktable for delivery.
		if (CurrentBucket && CarrySocket)
		{
			CurrentBucket->AttachToComponent(CarrySocket,
				FAttachmentTransformRules::SnapToTargetIncludingScale);
			CurrentBucket->SetActorRelativeLocation(FVector::ZeroVector);
		}
		if (ToSlotPtr.IsValid())
		{
			TargetLocation = ToSlotPtr->GetComponentLocation();
			TargetLocation.Z = GetActorLocation().Z;
		}
		break;
	case EWorkerState::Place:
		if (ToSlotPtr.IsValid()) DetachBucketAt(ToSlotPtr.Get());
		if (AssignedStation) OnPlaced.Broadcast(AssignedStation->NodeRef);
		EnterState(EWorkerState::ReturnHome);
		return;
	case EWorkerState::ReturnHome:
		TargetLocation = HomeLocation;
		break;
	case EWorkerState::Idle:
		break;
	}
}

UAnimSequence* AWorkerRobot::PickAnimationForState(EWorkerState QueryState) const
{
	const bool bMoving =
		QueryState == EWorkerState::MoveToInput   ||
		QueryState == EWorkerState::MoveToWorkPos ||
		QueryState == EWorkerState::MoveToOutput  ||
		QueryState == EWorkerState::ReturnHome;
	return bMoving ? WalkAnimation.Get() : IdleAnimation.Get();
}

void AWorkerRobot::RefreshAnimationForState()
{
	if (!SkeletalBodyMesh) return;
	UAnimSequence* Target = PickAnimationForState(State);
	if (!Target) return;
	// SetAnimation only stores the asset; SingleNodeInstance starts stopped, so
	// the mesh would freeze on frame 0. Play(true) kicks the looped playback.
	SkeletalBodyMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	SkeletalBodyMesh->SetAnimation(Target);
	SkeletalBodyMesh->Play(/*bLooping=*/true);
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
		if (WorkTimer >= WorkDuration)
		{
			if (AssignedStation && CurrentBucket)
			{
				TWeakObjectPtr<AWorkerRobot> WeakThis(this);
				APayloadCarrier* Bucket = CurrentBucket;
				AssignedStation->ProcessBucket(TArray<APayloadCarrier*>{Bucket},
					FStationProcessComplete::CreateLambda([WeakThis](FStationProcessResult Result)
					{
						if (AWorkerRobot* Self = WeakThis.Get())
						{
							Self->LastResult = Result;
							if (Self->AssignedStation)
							{
								Self->OnFinishedWorking.Broadcast(Self->AssignedStation->NodeRef);
							}
							Self->EnterState(EWorkerState::MoveToOutput);
						}
					}));
			}
			else
			{
				if (AssignedStation) OnFinishedWorking.Broadcast(AssignedStation->NodeRef);
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
			APayloadCarrier* Bucket = CurrentBucket;
			CurrentBucket = nullptr;
			TaskCompleteCb.ExecuteIfBound(Bucket);
		}
		break;
	default:
		break;
	}
}
