#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyLineGameMode.h"
#include "CinematicCameraDirector.h"
#include "Station.h"
#include "TestStations.h"
#include "WorkerRobot.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace AssemblyLineGameModeTests
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld(const TCHAR* Name)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				World->BeginTearingDown();
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

DEFINE_SPEC(FAssemblyLineGameModeSpec,
	"AssemblyLineSimul.AssemblyLineGameMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FAssemblyLineGameModeSpec::Define()
{
	using namespace AssemblyLineGameModeTests;

	Describe("SpawnAssemblyLine", [this]()
	{
		It("propagates WorkerRobotMeshAsset to every spawned worker", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_Propagation"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("GameMode spawned"), GM);
			if (!GM) return;

			USkeletalMesh* TestMesh = NewObject<USkeletalMesh>(GetTransientPackage());
			GM->WorkerRobotMeshAsset = TSoftObjectPtr<USkeletalMesh>(TestMesh);

			GM->SpawnAssemblyLine();

			int32 WorkerCount = 0;
			int32 PropagatedCount = 0;
			for (TActorIterator<AWorkerRobot> It(TW.World); It; ++It)
			{
				++WorkerCount;
				if (It->BodyMeshAsset.ToSoftObjectPath() == GM->WorkerRobotMeshAsset.ToSoftObjectPath())
				{
					++PropagatedCount;
				}
			}

			TestEqual(TEXT("Spawned 4 workers"), WorkerCount, 4);
			TestEqual(TEXT("All workers have GameMode's mesh asset"), PropagatedCount, 4);
		});

		It("propagates StationTalkWidgetClass to every spawned station", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_TalkWidgetPropagation"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("GameMode spawned"), GM);
			if (!GM) return;

			GM->StationTalkWidgetClass = UTestDerivedTalkWidget::StaticClass();
			GM->SpawnAssemblyLine();

			int32 StationCount = 0;
			int32 PropagatedCount = 0;
			for (TActorIterator<AStation> It(TW.World); It; ++It)
			{
				++StationCount;
				if (It->TalkWidgetClass.Get() == GM->StationTalkWidgetClass.Get())
				{
					++PropagatedCount;
				}
			}

			TestEqual(TEXT("Spawned 4 stations"), StationCount, 4);
			TestEqual(TEXT("All stations adopted GameMode's TalkWidgetClass"), PropagatedCount, 4);
		});
	});

	Describe("SpawnCinematicDirector", [this]()
	{
		It("spawns exactly one ACinematicCameraDirector with at least one shot configured", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_CinematicSpawn"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("GameMode spawned"), GM);
			if (!GM) return;

			GM->SpawnAssemblyLine();
			GM->SpawnCinematicDirector();

			int32 CinDirectorCount = 0;
			int32 ShotsConfigured = 0;
			for (TActorIterator<ACinematicCameraDirector> It(TW.World); It; ++It)
			{
				++CinDirectorCount;
				ShotsConfigured = It->Shots.Num();
			}

			TestEqual(TEXT("exactly one CinematicCameraDirector spawned"), CinDirectorCount, 1);
			TestTrue(TEXT("at least one shot configured"), ShotsConfigured > 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
