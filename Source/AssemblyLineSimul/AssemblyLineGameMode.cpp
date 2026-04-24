#include "AssemblyLineGameMode.h"
#include "AssemblyLineDirector.h"
#include "CinematicCameraDirector.h"
#include "Station.h"
#include "StationSubclasses.h"
#include "StationTalkWidget.h"
#include "WorkerRobot.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
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
	const FVector CheckerLoc = LineOrigin
		+ FVector(static_cast<float>(StationCount - 1) * StationSpacing, 0.f, 0.f);

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
	Cinematic->Shots.Add(MakeShot(LineCenter + FVector(-2200.f, 2200.f, 1600.f), LineCenter, 85.f, 6.f, 1.5f));
	Cinematic->Shots.Add(MakeShot(LineCenter + FVector(-1200.f, 1500.f,  800.f), LineCenter, 75.f, 4.f, 1.5f));
	Cinematic->Shots.Add(MakeShot(CheckerLoc + FVector(    0.f, 1000.f,  600.f), CheckerLoc, 60.f, 5.f, 1.5f));
	Cinematic->CheckerShotIndex = 2;
	Cinematic->ResumeShotIndex = 0;

	Cinematic->BindToAssemblyLine(Director);
	Cinematic->Start();
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
