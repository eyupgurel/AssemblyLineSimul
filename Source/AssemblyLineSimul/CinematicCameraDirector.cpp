#include "CinematicCameraDirector.h"
#include "AssemblyLineDirector.h"
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

ACinematicCameraDirector::ACinematicCameraDirector()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACinematicCameraDirector::Start()
{
	if (Shots.Num() == 0) return;
	EnsureShotCameras();
	JumpToShot(0);
	SetupSkipBinding();
}

void ACinematicCameraDirector::Stop()
{
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(ShotTimer);
	}
}

void ACinematicCameraDirector::AdvanceShot()
{
	if (Shots.Num() == 0) return;
	int32 NextIndex = CurrentShotIndex + 1;
	if (NextIndex >= Shots.Num())
	{
		NextIndex = bLoop ? 0 : Shots.Num() - 1;
	}
	JumpToShot(NextIndex);
}

void ACinematicCameraDirector::JumpToShot(int32 NewIndex)
{
	if (Shots.Num() == 0) return;
	NewIndex = FMath::Clamp(NewIndex, 0, Shots.Num() - 1);
	CurrentShotIndex = NewIndex;

	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(ShotTimer);
	}
	ApplyShot(NewIndex);
	ScheduleNextAdvance();
}

void ACinematicCameraDirector::BindToAssemblyLine(UAssemblyLineDirector* Director)
{
	if (UAssemblyLineDirector* Old = BoundDirector.Get())
	{
		Old->OnCheckerStarted.Remove(CheckerStartedHandle);
		Old->OnCycleCompleted.Remove(CycleCompletedHandle);
		Old->OnCycleRejected.Remove(CycleRejectedHandle);
	}
	BoundDirector = Director;
	if (!Director) return;
	CheckerStartedHandle = Director->OnCheckerStarted.AddUObject(this, &ACinematicCameraDirector::HandleCheckerStarted);
	CycleCompletedHandle = Director->OnCycleCompleted.AddUObject(this, &ACinematicCameraDirector::HandleCycleResumed);
	CycleRejectedHandle  = Director->OnCycleRejected .AddUObject(this, &ACinematicCameraDirector::HandleCycleResumed);
}

void ACinematicCameraDirector::ApplyShot(int32 Index)
{
	if (!Shots.IsValidIndex(Index)) return;
	UWorld* W = GetWorld();
	if (!W) return;
	APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
	if (!PC) return;
	if (!ShotCameras.IsValidIndex(Index) || !ShotCameras[Index]) return;
	PC->SetViewTargetWithBlend(ShotCameras[Index], Shots[Index].BlendDuration);
	if (UCameraComponent* CamComp = ShotCameras[Index]->GetCameraComponent())
	{
		CamComp->SetFieldOfView(Shots[Index].FieldOfView);
	}
}

void ACinematicCameraDirector::ScheduleNextAdvance()
{
	if (!Shots.IsValidIndex(CurrentShotIndex)) return;
	UWorld* W = GetWorld();
	if (!W) return;
	const float Hold = Shots[CurrentShotIndex].HoldDuration;
	if (Hold <= 0.f) return;
	W->GetTimerManager().SetTimer(
		ShotTimer, this, &ACinematicCameraDirector::AdvanceShot, Hold, false);
}

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

void ACinematicCameraDirector::HandleCheckerStarted()
{
	JumpToShot(CheckerShotIndex);
}

void ACinematicCameraDirector::HandleCycleResumed(ABucket* /*Bucket*/)
{
	JumpToShot(ResumeShotIndex);
}

void ACinematicCameraDirector::HandleSkipPressed()
{
	AdvanceShot();
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
	}
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(ShotTimer);
	}
	Super::EndPlay(EndPlayReason);
}
