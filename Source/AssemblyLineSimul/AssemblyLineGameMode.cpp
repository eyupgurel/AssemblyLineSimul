#include "AssemblyLineGameMode.h"
#include "AssemblyLineDirector.h"
#include "Station.h"
#include "StationSubclasses.h"
#include "WorkerRobot.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AAssemblyLineGameMode::AAssemblyLineGameMode()
{
	// We use the default game mode's pawn/controller for camera; the demo robots are spawned
	// separately and are not possessed by the player.
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

	UAssemblyLineDirector* Director = World->GetSubsystem<UAssemblyLineDirector>();
	if (!Director) return;

	struct FStationSpec { TSubclassOf<AStation> Class; };
	const TArray<FStationSpec> Specs = {
		{ AGeneratorStation::StaticClass() },
		{ AFilterStation::StaticClass()    },
		{ ASorterStation::StaticClass()    },
		{ ACheckerStation::StaticClass()   }
	};

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 i = 0; i < Specs.Num(); ++i)
	{
		const FVector Loc = LineOrigin + FVector((float)i * StationSpacing, 0.f, 0.f);
		AStation* Station = World->SpawnActor<AStation>(Specs[i].Class, Loc, FRotator::ZeroRotator, Params);
		if (!Station) continue;
		Director->RegisterStation(Station);

		const FVector RobotLoc = Station->WorkerStandPoint
			? Station->WorkerStandPoint->GetComponentLocation()
			: Loc + FVector(-250.f, 0.f, 0.f);

		AWorkerRobot* Robot = World->SpawnActor<AWorkerRobot>(AWorkerRobot::StaticClass(), RobotLoc, FRotator(0.f, 0.f, 0.f), Params);
		if (Robot)
		{
			Robot->AssignStation(Station);
			Director->RegisterRobot(Robot);
		}
	}

	// Spawn a fixed cinematic camera that frames the whole line, and force the
	// player's view to it. Without this, the default ADefaultPawn spawns at the
	// world origin looking +X and the player sees the first robot for ~1 frame
	// before it walks off-camera.
	const FVector LineCenter = LineOrigin
		+ FVector((float)(Specs.Num() - 1) * 0.5f * StationSpacing, 0.f, 0.f);
	const FVector CameraLoc = LineCenter + FVector(-2200.f, 2200.f, 1600.f);
	ACameraActor* Cam = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), CameraLoc, FRotator::ZeroRotator, Params);
	if (Cam)
	{
		Cam->SetActorRotation((LineCenter - CameraLoc).Rotation());
		if (UCameraComponent* CamComp = Cam->GetCameraComponent())
		{
			CamComp->SetFieldOfView(85.f);
		}
	}

	// Defer view-target swap + cycle start until the player controller exists.
	FTimerHandle Th;
	World->GetTimerManager().SetTimer(Th,
		FTimerDelegate::CreateLambda([this, World, Director, Cam]()
		{
			if (Cam)
			{
				if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
				{
					PC->SetViewTargetWithBlend(Cam, 0.0f);
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Green,
							TEXT("View target set to assembly-line camera"));
					}
				}
			}
			Director->StartCycle();
		}),
		1.5f, false);
}
