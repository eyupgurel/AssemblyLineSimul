#include "CinematicCameraDirector.h"
#include "AssemblyLineDirector.h"
#include "Bucket.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "WorkerRobot.h"

namespace
{
	// Hardcoded chase offset (Story 16); Story 36 keeps it as a constant
	// rather than promoting to UPROPERTY since no scene needs to override it.
	const FVector ChaseOffsetFromBucket(-180.f, 320.f, 220.f);
	constexpr float ChaseFOV = 55.f;
	constexpr float ChaseEnterBlend = 0.6f;
}

ACinematicCameraDirector::ACinematicCameraDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;  // every frame for smooth follow
}

void ACinematicCameraDirector::Start()
{
	EnsureShotCameras();
	EnsureFollowCamera();
	JumpToWideOverview();
	SetupSkipBinding();
}

void ACinematicCameraDirector::Stop()
{
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(IdleLingerTimer);
	}
}

void ACinematicCameraDirector::BindToAssemblyLine(UAssemblyLineDirector* Director)
{
	if (UAssemblyLineDirector* Old = BoundDirector.Get())
	{
		Old->OnCheckerStarted.Remove(CheckerStartedHandle);
		Old->OnCycleCompleted.Remove(CycleCompletedHandle);
		Old->OnCycleRejected.Remove(CycleRejectedHandle);
		Old->OnStationActive.Remove(StationActiveHandle);
		Old->OnStationIdle.Remove(StationIdleHandle);
	}
	BoundDirector = Director;
	if (!Director) return;
	// Intentionally NOT binding OnCheckerStarted: the early-anticipation jump
	// made the Sorter→Checker handoff inconsistent. Treat Checker like any
	// other station — follow window starts when the worker enters Working.
	CycleCompletedHandle = Director->OnCycleCompleted.AddUObject(this, &ACinematicCameraDirector::HandleCycleResumed);
	CycleRejectedHandle  = Director->OnCycleRejected .AddUObject(this, &ACinematicCameraDirector::HandleCycleRejected);
	StationActiveHandle  = Director->OnStationActive .AddUObject(this, &ACinematicCameraDirector::HandleStationActive);
	StationIdleHandle    = Director->OnStationIdle   .AddUObject(this, &ACinematicCameraDirector::HandleStationIdle);
}

ABucket* ACinematicCameraDirector::GetChaseTarget() const
{
	if (Mode == ECinematicMode::ChasingBucket)
	{
		return Cast<ABucket>(FollowSubject.Get());
	}
	return nullptr;
}

// --- mode entries -----------------------------------------------------------

void ACinematicCameraDirector::EnterFollowingBucket(AActor* Subject, EStationType Kind)
{
	if (!Subject)
	{
		// No subject — keep current mode; null-bucket fallback happens
		// at HandleStationActive level by simply not entering this mode.
		return;
	}

	UWorld* W = GetWorld();
	if (!W) return;

	// Most-recent-subject tiebreak (AC36.9). Even if we're already
	// following a different bucket, switching freshly resets the
	// sequence + start time so the new subject gets the full
	// wide-to-close zoom dance.
	Mode               = ECinematicMode::FollowingBucket;
	FollowSubject      = Subject;
	ActiveSequence     = ResolveSequenceForKind(Kind);
	ElapsedInFollowMode = 0.f;

	// Cancel any pending linger-back-to-overview timer left over from
	// a recently-Idle station — we just got a fresh active subject.
	W->GetTimerManager().ClearTimer(IdleLingerTimer);

	EnsureFollowCamera();
	if (!FollowCamera) return;

	// Place the camera at the FIRST keyframe's offset (or at the
	// subject's location with a neutral offset if no keyframes) so the
	// blend looks right immediately.
	FVector InitialOffset = FVector(0.f, 250.f, 500.f);
	float   InitialFOV    = 45.f;
	float   InitialBlend  = 0.6f;
	if (ActiveSequence.Keyframes.Num() > 0)
	{
		InitialOffset = ActiveSequence.Keyframes[0].Offset;
		InitialFOV    = ActiveSequence.Keyframes[0].FOV;
		InitialBlend  = ActiveSequence.Keyframes[0].BlendTime;
	}

	const FVector  SubjectLoc = Subject->GetActorLocation();
	const FVector  CamLoc     = SubjectLoc + InitialOffset;
	const FRotator LookAt     = (SubjectLoc - CamLoc).Rotation();
	FollowCamera->SetActorLocationAndRotation(CamLoc, LookAt);
	if (UCameraComponent* CamComp = FollowCamera->GetCameraComponent())
	{
		CamComp->SetFieldOfView(InitialFOV);
	}
	LastAppliedOffset = InitialOffset;
	LastAppliedFOV    = InitialFOV;

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0))
	{
		PC->SetViewTargetWithBlend(FollowCamera, InitialBlend);
	}
}

void ACinematicCameraDirector::EnterChase(ABucket* Bucket)
{
	UWorld* W = GetWorld();
	if (!W) return;

	if (!Bucket)
	{
		// Null-bucket fallback — snap back to overview.
		EnterWideOverview();
		return;
	}

	Mode          = ECinematicMode::ChasingBucket;
	FollowSubject = Bucket;
	W->GetTimerManager().ClearTimer(IdleLingerTimer);

	EnsureFollowCamera();
	if (!FollowCamera) return;

	const FVector  BucketLoc = Bucket->GetActorLocation();
	const FVector  CamLoc    = BucketLoc + ChaseOffsetFromBucket;
	const FRotator LookAt    = (BucketLoc - CamLoc).Rotation();
	FollowCamera->SetActorLocationAndRotation(CamLoc, LookAt);
	if (UCameraComponent* CamComp = FollowCamera->GetCameraComponent())
	{
		CamComp->SetFieldOfView(ChaseFOV);
	}
	LastAppliedOffset = ChaseOffsetFromBucket;
	LastAppliedFOV    = ChaseFOV;

	UE_LOG(LogTemp, Display,
		TEXT("[Chase] enter — bucket at %s, camera placed at %s"),
		*BucketLoc.ToString(), *CamLoc.ToString());

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0))
	{
		PC->SetViewTargetWithBlend(FollowCamera, ChaseEnterBlend);
	}
}

void ACinematicCameraDirector::EnterWideOverview()
{
	Mode = ECinematicMode::WideOverview;
	FollowSubject.Reset();
	JumpToWideOverview();
}

void ACinematicCameraDirector::JumpToWideOverview()
{
	if (Shots.Num() == 0) return;
	const int32 Index = FMath::Clamp(ResumeShotIndex, 0, Shots.Num() - 1);
	CurrentShotIndex = Index;

	UWorld* W = GetWorld();
	if (!W) return;
	if (!ShotCameras.IsValidIndex(Index) || !ShotCameras[Index]) return;

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0))
	{
		PC->SetViewTargetWithBlend(ShotCameras[Index], Shots[Index].BlendDuration);
	}
	if (UCameraComponent* CamComp = ShotCameras[Index]->GetCameraComponent())
	{
		CamComp->SetFieldOfView(Shots[Index].FieldOfView);
	}
}

// --- event handlers ---------------------------------------------------------

void ACinematicCameraDirector::HandleCheckerStarted()
{
	// Story 36 — no-op. The Checker's follow window starts when its worker
	// enters Working (HandleStationActive), same as every other station.
}

void ACinematicCameraDirector::HandleCycleRejected(ABucket* Bucket)
{
	// Story 16 AC16.1 — chase the rejected bucket back to its rework station.
	EnterChase(Bucket);
}

void ACinematicCameraDirector::HandleCycleResumed(ABucket* Bucket)
{
	// PASS path: chase the accepted bucket too — gives the audience a clean
	// "victory beat" close-up before it vanishes and the next cycle begins.
	EnterChase(Bucket);
}

void ACinematicCameraDirector::HandleStationActive(const FNodeRef& Ref)
{
	UAssemblyLineDirector* Director = BoundDirector.Get();
	if (!Director) return;

	AWorkerRobot* Robot = Director->GetRobotByNodeRef(Ref);
	if (!Robot)
	{
		// No worker for this NodeRef (multi-instance bug if it happens).
		// Don't crash; stay in current mode.
		return;
	}

	ABucket* Bucket = Robot->GetCurrentBucket();
	if (!Bucket)
	{
		// No bucket to follow yet — stay in current mode. The next
		// HandleStationActive (or chase event) will pick us up.
		return;
	}

	EnterFollowingBucket(Bucket, Ref.Kind);
}

void ACinematicCameraDirector::HandleStationIdle(const FNodeRef& /*Ref*/)
{
	UWorld* W = GetWorld();
	if (LingerSecondsAfterIdle <= 0.f || !W)
	{
		EnterWideOverview();
		return;
	}
	W->GetTimerManager().ClearTimer(IdleLingerTimer);
	W->GetTimerManager().SetTimer(IdleLingerTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]() { EnterWideOverview(); }),
		LingerSecondsAfterIdle, false);
}

// --- camera infrastructure --------------------------------------------------

void ACinematicCameraDirector::EnsureShotCameras()
{
	if (ShotCameras.Num() == Shots.Num()) return;
	UWorld* W = GetWorld();
	if (!W) return;

	for (ACameraActor* Cam : ShotCameras)
	{
		if (Cam) Cam->Destroy();
	}
	ShotCameras.Reset(Shots.Num());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;

	for (const FCinematicShot& Shot : Shots)
	{
		ACameraActor* Cam = W->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), Shot.Location, Shot.Rotation, Params);
		if (Cam)
		{
			if (UCameraComponent* CamComp = Cam->GetCameraComponent())
			{
				CamComp->SetFieldOfView(Shot.FieldOfView);
			}
		}
		ShotCameras.Add(Cam);
	}
}

void ACinematicCameraDirector::EnsureFollowCamera()
{
	if (FollowCamera) return;
	UWorld* W = GetWorld();
	if (!W) return;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;
	FollowCamera = W->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (FollowCamera)
	{
		if (UCameraComponent* CamComp = FollowCamera->GetCameraComponent())
		{
			CamComp->SetFieldOfView(60.f);
		}
	}
}

const FFramingSequence& ACinematicCameraDirector::ResolveSequenceForKind(EStationType Kind) const
{
	if (const FFramingSequence* Override = FramingByKind.Find(Kind))
	{
		return *Override;
	}
	return DefaultFollowSequence;
}

// --- tick -------------------------------------------------------------------

void ACinematicCameraDirector::TickFollowCamera()
{
	AActor* Subject = FollowSubject.Get();
	if (!Subject)
	{
		// Subject vanished (PASS-path destroy, recycle, actor cleanup) —
		// fall back to wide overview.
		EnterWideOverview();
		return;
	}

	if (!FollowCamera) return;

	// While following, also keep slamming the view target back to the
	// follow camera — cheap belt-and-suspenders against any stray timer
	// or input that might snap the view away. SetViewTargetWithBlend is
	// a no-op when the new target equals the current target.
	if (UWorld* W = GetWorld())
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0))
		{
			if (PC->GetViewTarget() != FollowCamera)
			{
				PC->SetViewTargetWithBlend(FollowCamera, /*BlendTime=*/0.f);
			}
		}
	}

	FVector TargetOffset;
	float   TargetFOV;

	if (Mode == ECinematicMode::ChasingBucket)
	{
		TargetOffset = ChaseOffsetFromBucket;
		TargetFOV    = ChaseFOV;
	}
	else  // FollowingBucket
	{
		// Resolve active keyframe by elapsed time. The active keyframe
		// is the latest one whose Time <= elapsed. We blend offset/FOV
		// from the prior keyframe to the active one over the active
		// keyframe's BlendTime — produces the wide → mid → close zoom
		// dance.
		const float Elapsed = ElapsedInFollowMode;

		const TArray<FFramingKeyframe>& Keys = ActiveSequence.Keyframes;
		if (Keys.Num() == 0)
		{
			// No sequence — keep the initial framing applied at entry.
			TargetOffset = LastAppliedOffset;
			TargetFOV    = LastAppliedFOV;
		}
		else
		{
			// Find the active index (largest i with Keys[i].Time <= Elapsed).
			int32 ActiveIdx = 0;
			for (int32 i = 0; i < Keys.Num(); ++i)
			{
				if (Keys[i].Time <= Elapsed) ActiveIdx = i;
				else break;
			}

			const FFramingKeyframe& Active = Keys[ActiveIdx];
			if (ActiveIdx == 0)
			{
				// No prior keyframe to blend from — use the active one verbatim.
				TargetOffset = Active.Offset;
				TargetFOV    = Active.FOV;
			}
			else
			{
				const FFramingKeyframe& Prior = Keys[ActiveIdx - 1];
				const float TimeIntoActive = Elapsed - Active.Time;
				const float Alpha = Active.BlendTime > 0.f
					? FMath::Clamp(TimeIntoActive / Active.BlendTime, 0.f, 1.f)
					: 1.f;
				TargetOffset = FMath::Lerp(Prior.Offset, Active.Offset, Alpha);
				TargetFOV    = FMath::Lerp(Prior.FOV,    Active.FOV,    Alpha);
			}
		}
	}

	const FVector  SubjectLoc = Subject->GetActorLocation();
	const FVector  CamLoc     = SubjectLoc + TargetOffset;
	const FRotator LookAt     = (SubjectLoc - CamLoc).Rotation();
	FollowCamera->SetActorLocationAndRotation(CamLoc, LookAt);

	if (UCameraComponent* CamComp = FollowCamera->GetCameraComponent())
	{
		CamComp->SetFieldOfView(TargetFOV);
	}

	LastAppliedOffset = TargetOffset;
	LastAppliedFOV    = TargetFOV;
}

void ACinematicCameraDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Mode == ECinematicMode::WideOverview) return;
	if (Mode == ECinematicMode::FollowingBucket)
	{
		ElapsedInFollowMode += DeltaSeconds;
	}
	TickFollowCamera();
}

// --- input ------------------------------------------------------------------

void ACinematicCameraDirector::HandleSkipPressed()
{
	// Story 36 — Skip now means "back to wide overview" (the static-shot
	// advance path is gone). If we're chasing or following, this lets the
	// operator yank back to the overview shot.
	EnterWideOverview();
}

void ACinematicCameraDirector::SetupSkipBinding()
{
	UWorld* W = GetWorld();
	if (!W) return;
	APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
	if (!PC) return;

	if (!SkipAction)
	{
		SkipAction = NewObject<UInputAction>(this, TEXT("SkipAction"));
		SkipAction->ValueType = EInputActionValueType::Boolean;
	}
	if (!SkipMappingContext)
	{
		SkipMappingContext = NewObject<UInputMappingContext>(this, TEXT("SkipMappingContext"));
		SkipMappingContext->MapKey(SkipAction, EKeys::SpaceBar);
	}

	if (ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys =
				LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsys->AddMappingContext(SkipMappingContext, 0);
		}
	}

	EnableInput(PC);
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(SkipAction, ETriggerEvent::Triggered,
			this, &ACinematicCameraDirector::HandleSkipPressed);
	}
}

void ACinematicCameraDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAssemblyLineDirector* D = BoundDirector.Get())
	{
		D->OnCheckerStarted.Remove(CheckerStartedHandle);
		D->OnCycleCompleted.Remove(CycleCompletedHandle);
		D->OnCycleRejected.Remove(CycleRejectedHandle);
		D->OnStationActive.Remove(StationActiveHandle);
		D->OnStationIdle.Remove(StationIdleHandle);
	}
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(IdleLingerTimer);
	}
	Super::EndPlay(EndPlayReason);
}
