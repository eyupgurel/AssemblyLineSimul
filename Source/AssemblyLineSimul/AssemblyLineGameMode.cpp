#include "AssemblyLineGameMode.h"
#include "AgentChatSubsystem.h"
#include "AgentChatWidget.h"
#include "AssemblyLineDirector.h"
#include "AssemblyLineFeedback.h"
#include "CinematicCameraDirector.h"
#include "MacAudioCapture.h"
#include "OpenAIAPISubsystem.h"
#include "Station.h"
#include "StationSubclasses.h"
#include "StationTalkWidget.h"
#include "VoiceSubsystem.h"
#include "WorkerRobot.h"
#include "Blueprint/UserWidget.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
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

void AAssemblyLineGameMode::SpawnFloor()
{
	if (FloorMesh.IsNull()) return;
	UStaticMesh* Mesh = FloorMesh.LoadSynchronous();
	if (!Mesh) return;

	UWorld* World = GetWorld();
	if (!World) return;

	const FVector MeshSize = Mesh->GetBoundingBox().GetSize();
	const float TileSizeX = MeshSize.X * FloorScale.X;
	const float TileSizeY = MeshSize.Y * FloorScale.Y;
	if (TileSizeX <= 0.f || TileSizeY <= 0.f) return;

	const int32 StationCount = 4;
	const float LineSpan = static_cast<float>(StationCount - 1) * StationSpacing;
	const FVector LineCenter = LineOrigin + FVector(LineSpan * 0.5f, 0.f, 0.f);
	// Worker capsule half-height 90 cm × actor scale 1.5 → feet at LineOrigin.Z - 135.
	const float FloorZ = LineOrigin.Z - 135.f + FloorOffset.Z;

	const FVector2D BoundsCenter(LineCenter.X + FloorOffset.X, LineCenter.Y + FloorOffset.Y);
	const int32 TilesX = FloorTilesX;
	const int32 TilesY = FloorTilesY;
	const float OriginX = BoundsCenter.X - 0.5f * (TilesX - 1) * TileSizeX;
	const float OriginY = BoundsCenter.Y - 0.5f * (TilesY - 1) * TileSizeY;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Deferred so we can flip the SMC to Movable mobility and swap in the mesh
	// before the actor finishes constructing — required for runtime mesh swap.
	Params.bDeferConstruction = true;

	for (int32 IX = 0; IX < TilesX; ++IX)
	{
		for (int32 IY = 0; IY < TilesY; ++IY)
		{
			const FVector TileLoc(
				OriginX + static_cast<float>(IX) * TileSizeX,
				OriginY + static_cast<float>(IY) * TileSizeY,
				FloorZ);

			AStaticMeshActor* Tile = World->SpawnActor<AStaticMeshActor>(
				AStaticMeshActor::StaticClass(), TileLoc, FRotator::ZeroRotator, Params);
			if (!Tile) continue;
			Tile->Tags.AddUnique(TEXT("AssemblyLineFloor"));
			if (UStaticMeshComponent* Smc = Tile->GetStaticMeshComponent())
			{
				Smc->SetMobility(EComponentMobility::Movable);
				Smc->SetStaticMesh(Mesh);
				Smc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			Tile->FinishSpawning(FTransform(FRotator::ZeroRotator, TileLoc, FloorScale));
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

void AAssemblyLineGameMode::SetupVoiceInput()
{
	UWorld* World = GetWorld();
	if (!World) return;
	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (!PC) return;

	if (!VoiceTalkAction)
	{
		VoiceTalkAction = NewObject<UInputAction>(this, TEXT("VoiceTalkAction"));
		VoiceTalkAction->ValueType = EInputActionValueType::Boolean;
	}
	if (!VoiceMappingContext)
	{
		VoiceMappingContext = NewObject<UInputMappingContext>(this, TEXT("VoiceMappingContext"));
		VoiceMappingContext->MapKey(VoiceTalkAction, EKeys::SpaceBar);
	}
	if (ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys =
				LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsys->AddMappingContext(VoiceMappingContext, 1);
		}
	}
	EnableInput(PC);
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(VoiceTalkAction, ETriggerEvent::Started,
			this, &AAssemblyLineGameMode::OnVoiceTalkStarted);
		EIC->BindAction(VoiceTalkAction, ETriggerEvent::Completed,
			this, &AAssemblyLineGameMode::OnVoiceTalkCompleted);
	}

	// Subscribe to active-agent changes so we can light up the right station and
	// have it speak its affirmation when hailed.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UVoiceSubsystem* Voice = GI->GetSubsystem<UVoiceSubsystem>())
		{
			ActiveAgentChangedHandle = Voice->OnActiveAgentChanged.AddUObject(
				this, &AAssemblyLineGameMode::HandleActiveAgentChanged);
		}
	}
}

void AAssemblyLineGameMode::OnVoiceTalkStarted()
{
	if (!AudioCapture)
	{
		AudioCapture = NewObject<UMacAudioCapture>(this);
	}
	if (AudioCapture->IsRecording())
	{
		return;  // already recording (auto-repeat key spam)
	}
	if (!AudioCapture->BeginRecord())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red,
				TEXT("Voice: failed to start recording (mic permission?)"));
		}
		return;
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(/*Key=*/42, 30.f, FColor::Red, TEXT("● REC"));
	}
}

void AAssemblyLineGameMode::OnVoiceTalkCompleted()
{
	if (!AudioCapture || !AudioCapture->IsRecording()) return;

	TArray<uint8> AudioBytes;
	FString MimeType, FilenameHint;
	const bool bRead = AudioCapture->EndRecord(AudioBytes, MimeType, FilenameHint);
	if (GEngine) GEngine->RemoveOnScreenDebugMessage(42);
	if (!bRead) return;

	UGameInstance* GI = GetGameInstance();
	UOpenAIAPISubsystem* OpenAI = GI ? GI->GetSubsystem<UOpenAIAPISubsystem>() : nullptr;
	if (!OpenAI || !OpenAI->HasAPIKey())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
				TEXT("Voice: no OpenAI key — drop one in Content/Secrets/OpenAIAPIKey.txt"));
		}
		return;
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(43, 5.f, FColor::Yellow, TEXT("Transcribing…"));
	}
	TWeakObjectPtr<AAssemblyLineGameMode> Weak(this);
	OpenAI->TranscribeAudio(AudioBytes, MimeType, FilenameHint,
		FWhisperComplete::CreateLambda([Weak](bool bSuccess, const FString& Transcript)
		{
			AAssemblyLineGameMode* Self = Weak.Get();
			if (!Self) return;
			if (GEngine) GEngine->RemoveOnScreenDebugMessage(43);
			if (!bSuccess)
			{
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
						FString::Printf(TEXT("Whisper failed: %s"), *Transcript));
				}
				return;
			}
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Green,
					FString::Printf(TEXT("\"%s\""), *Transcript));
			}
			if (UGameInstance* GI = Self->GetGameInstance())
			{
				if (UVoiceSubsystem* Voice = GI->GetSubsystem<UVoiceSubsystem>())
				{
					Voice->HandleTranscript(Transcript);
				}
			}
		}));
}

void AAssemblyLineGameMode::HandleActiveAgentChanged(EStationType Agent)
{
	UWorld* World = GetWorld();
	if (!World) return;
	UAssemblyLineDirector* Director = World->GetSubsystem<UAssemblyLineDirector>();
	if (!Director) return;

	// Story 19 — light up the WORKER for the active agent (green glow), not the
	// station (which used to glow cyan). Speak the affirmation through the
	// active agent's talk panel + macOS-say pipeline below.
	const TArray<EStationType> All = {
		EStationType::Generator, EStationType::Filter, EStationType::Sorter, EStationType::Checker
	};
	for (EStationType T : All)
	{
		if (AWorkerRobot* Worker = Director->GetRobotForStation(T))
		{
			Worker->SetActive(T == Agent);
		}
	}
	if (AStation* Active = Director->GetStationOfType(Agent))
	{
		const TCHAR* FriendlyName = TEXT("Agent");
		switch (Agent)
		{
		case EStationType::Generator: FriendlyName = TEXT("Generator"); break;
		case EStationType::Filter:    FriendlyName = TEXT("Filter");    break;
		case EStationType::Sorter:    FriendlyName = TEXT("Sorter");    break;
		case EStationType::Checker:   FriendlyName = TEXT("Checker");   break;
		}
		const FString Affirmation = FString::Printf(
			TEXT("%s here, reading you loud and clear. Go ahead."), FriendlyName);
		Active->SpeakStreaming(Affirmation);

		// Also push through the macOS `say` pipeline so the audience HEARS the
		// handshake — SpeakStreaming alone only updates the talk panel text.
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAgentChatSubsystem* Chat = GI->GetSubsystem<UAgentChatSubsystem>())
			{
				Chat->SpeakResponse(Affirmation);
			}
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
	SpawnFloor();
	SpawnCinematicDirector();
	SpawnFeedback();
	SpawnChatWidget();
	SetupVoiceInput();

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
