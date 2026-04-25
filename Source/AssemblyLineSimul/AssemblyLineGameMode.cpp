#include "AssemblyLineGameMode.h"
#include "AgentChatWidget.h"
#include "AssemblyLineDirector.h"
#include "AssemblyLineFeedback.h"
#include "CinematicCameraDirector.h"
#include "Station.h"
#include "StationSubclasses.h"
#include "StationTalkWidget.h"
#include "WorkerRobot.h"
#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AAssemblyLineGameMode::AAssemblyLineGameMode()
{
	// Default game mode pawn/controller is fine; demo robots are spawned separately
	// and are not possessed by the player.
}

void AAssemblyLineGameMode::SpawnAssemblyLine()
{
	UWorld* World = GetWorld();
	if (!World) return;

	UAssemblyLineDirector* Director = World->GetSubsystem<UAssemblyLineDirector>();
	if (!Director) return;

	if (BucketClass)
	{
		Director->BucketClass = BucketClass;
	}

	const TArray<TSubclassOf<AStation>> Specs = {
		AGeneratorStation::StaticClass(),
		AFilterStation::StaticClass(),
		ASorterStation::StaticClass(),
		ACheckerStation::StaticClass()
	};

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 i = 0; i < Specs.Num(); ++i)
	{
		const FVector Loc = LineOrigin + FVector((float)i * StationSpacing, 0.f, 0.f);
		AStation* Station = World->SpawnActor<AStation>(Specs[i], Loc, FRotator::ZeroRotator, Params);
		if (!Station) continue;
		if (StationTalkWidgetClass)
		{
			Station->TalkWidgetClass = StationTalkWidgetClass;
		}
		Director->RegisterStation(Station);

		const FVector RobotLoc = Station->WorkerStandPoint
			? Station->WorkerStandPoint->GetComponentLocation()
			: Loc + FVector(-250.f, 0.f, 0.f);

		AWorkerRobot* Robot = World->SpawnActor<AWorkerRobot>(
			AWorkerRobot::StaticClass(), RobotLoc, FRotator::ZeroRotator, Params);
		if (Robot)
		{
			Robot->AssignStation(Station);
			// Generator skips the WorkTimer wait — its ProcessBucket fills + holds, so the
			// camera doesn't sit on an empty table.
			Robot->WorkDuration = (Station->StationType == EStationType::Generator)
				? 0.f
				: StationWorkDuration;
			Robot->BodyMeshAsset = WorkerRobotMeshAsset;
			Robot->LoadAndApplyBodyMesh();
			if (const FLinearColor* Tint = RobotTintByStation.Find(Station->StationType))
			{
				Robot->ApplyTint(*Tint);
			}
			Director->RegisterRobot(Robot);
		}
	}
}

void AAssemblyLineGameMode::SpawnCinematicDirector()
{
	UWorld* World = GetWorld();
	if (!World) return;
	UAssemblyLineDirector* Director = World->GetSubsystem<UAssemblyLineDirector>();
	if (!Director) return;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACinematicCameraDirector* Cinematic = World->SpawnActor<ACinematicCameraDirector>(
		ACinematicCameraDirector::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!Cinematic) return;

	const int32 StationCount = 4;
	const FVector LineCenter = LineOrigin
		+ FVector(static_cast<float>(StationCount - 1) * 0.5f * StationSpacing, 0.f, 0.f);

	auto StationLoc = [&](int32 Idx)
	{
		return LineOrigin + FVector(static_cast<float>(Idx) * StationSpacing, 0.f, 0.f);
	};

	auto MakeShot = [](const FVector& Loc, const FVector& LookAt, float FOV, float Hold, float Blend)
	{
		FCinematicShot Shot;
		Shot.Location = Loc;
		Shot.Rotation = (LookAt - Loc).Rotation();
		Shot.FieldOfView = FOV;
		Shot.HoldDuration = Hold;
		Shot.BlendDuration = Blend;
		return Shot;
	};

	Cinematic->Shots.Reset();
	// 0: wide overview (also the resume shot). Slow blend out of closeups for relaxed pacing.
	Cinematic->Shots.Add(MakeShot(LineCenter + FVector(-2200.f, 2200.f, 1600.f), LineCenter, 85.f, 8.f, 2.5f));
	// 1..4: high-angle overhead per station — camera tilted ~57° down from a slight Y offset
	// so we look at the bucket sitting on the worktable. Bucket dock is at S + (0, 0, 120ish);
	// camera at S + (0, 250, 500) → distance ~460cm, FOV 45° → ~380cm view width.
	for (int32 i = 0; i < StationCount; ++i)
	{
		const FVector S = StationLoc(i);
		const FVector TableTop = S + FVector(0.f, 0.f, 120.f);
		Cinematic->Shots.Add(MakeShot(S + FVector(0.f, 250.f, 500.f), TableTop, 45.f, 25.f, 2.5f));
	}

	Cinematic->StationCloseupShotIndex.Reset();
	Cinematic->StationCloseupShotIndex.Add(EStationType::Generator, 1);
	Cinematic->StationCloseupShotIndex.Add(EStationType::Filter,    2);
	Cinematic->StationCloseupShotIndex.Add(EStationType::Sorter,    3);
	Cinematic->StationCloseupShotIndex.Add(EStationType::Checker,   4);
	Cinematic->CheckerShotIndex = 4;
	Cinematic->ResumeShotIndex  = 0;
	// Zoom out the moment Working ends so the audience sees the bucket get carried from the
	// table (instead of staying on the closeup through the carry).
	Cinematic->LingerSecondsAfterIdle = 0.f;

	Cinematic->BindToAssemblyLine(Director);
	Cinematic->Start();
}

void AAssemblyLineGameMode::SpawnFeedback()
{
	UWorld* World = GetWorld();
	if (!World) return;
	UAssemblyLineDirector* Director = World->GetSubsystem<UAssemblyLineDirector>();
	if (!Director) return;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAssemblyLineFeedback* Feedback = World->SpawnActor<AAssemblyLineFeedback>(
		AAssemblyLineFeedback::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Feedback)
	{
		Feedback->BindToAssemblyLine(Director);
	}
}

void AAssemblyLineGameMode::SpawnChatWidget()
{
	UWorld* World = GetWorld();
	if (!World) return;
	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (!PC) return;

	ChatWidget = CreateWidget<UAgentChatWidget>(PC, UAgentChatWidget::StaticClass());
	if (!ChatWidget) return;
	ChatWidget->AddToViewport(100);
	ChatWidget->SetVisibility(ESlateVisibility::Hidden);

	// Programmatic Enhanced Input binding for Tab → ToggleChatWidget.
	if (!ChatToggleAction)
	{
		ChatToggleAction = NewObject<UInputAction>(this, TEXT("ChatToggleAction"));
		ChatToggleAction->ValueType = EInputActionValueType::Boolean;
	}
	if (!ChatToggleMappingContext)
	{
		ChatToggleMappingContext = NewObject<UInputMappingContext>(this, TEXT("ChatToggleMappingContext"));
		ChatToggleMappingContext->MapKey(ChatToggleAction, EKeys::Tab);
	}
	if (ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys =
				LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsys->AddMappingContext(ChatToggleMappingContext, 1);
		}
	}
	EnableInput(PC);
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(ChatToggleAction, ETriggerEvent::Triggered,
			this, &AAssemblyLineGameMode::ToggleChatWidget);
	}
}

void AAssemblyLineGameMode::ToggleChatWidget()
{
	if (!ChatWidget) return;
	const bool bWasVisible = ChatWidget->IsVisible();
	ChatWidget->SetVisibility(bWasVisible ? ESlateVisibility::Hidden : ESlateVisibility::Visible);

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (!bWasVisible)
		{
			FInputModeGameAndUI Mode;
			Mode.SetWidgetToFocus(ChatWidget->TakeWidget());
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PC->SetInputMode(Mode);
			PC->bShowMouseCursor = true;
		}
		else
		{
			FInputModeGameOnly Mode;
			PC->SetInputMode(Mode);
			PC->bShowMouseCursor = false;
		}
	}
}

void AAssemblyLineGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World) return;

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Yellow,
			TEXT("AssemblyLineGameMode running"));
	}

	SpawnAssemblyLine();
	SpawnCinematicDirector();
	SpawnFeedback();
	SpawnChatWidget();

	UAssemblyLineDirector* Director = World->GetSubsystem<UAssemblyLineDirector>();
	if (!Director) return;

	FTimerHandle Th;
	World->GetTimerManager().SetTimer(Th,
		FTimerDelegate::CreateLambda([Director]()
		{
			Director->StartCycle();
		}),
		1.5f, false);
}
