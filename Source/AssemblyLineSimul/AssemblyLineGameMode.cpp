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
	// Default game mode pawn/controller is fine; demo robots are spawned separately
	// and are not possessed by the player.
}

void AAssemblyLineGameMode::SpawnAssemblyLine()
{
	UWorld* World = GetWorld();
	if (!World) return;

	UAssemblyLineDirector* Director = World->GetSubsystem<UAssemblyLineDirector>();
	if (!Director) return;

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
		Director->RegisterStation(Station);

		const FVector RobotLoc = Station->WorkerStandPoint
			? Station->WorkerStandPoint->GetComponentLocation()
			: Loc + FVector(-250.f, 0.f, 0.f);

		AWorkerRobot* Robot = World->SpawnActor<AWorkerRobot>(
			AWorkerRobot::StaticClass(), RobotLoc, FRotator::ZeroRotator, Params);
		if (Robot)
		{
			Robot->AssignStation(Station);
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

	UAssemblyLineDirector* Director = World->GetSubsystem<UAssemblyLineDirector>();
	if (!Director) return;

	// Cinematic camera framing the whole line; player view is forced to it once the
	// player controller exists.
	const int32 StationCount = 4;
	const FVector LineCenter = LineOrigin
		+ FVector((float)(StationCount - 1) * 0.5f * StationSpacing, 0.f, 0.f);
	const FVector CameraLoc = LineCenter + FVector(-2200.f, 2200.f, 1600.f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
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
