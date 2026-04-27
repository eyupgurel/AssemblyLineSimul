#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyLineDirector.h"
#include "AssemblyLineGameMode.h"
#include "CinematicCameraDirector.h"
#include "Station.h"
#include "TestStations.h"
#include "WorkerRobot.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
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
		It("propagates BucketClass to the Director", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_BucketClassPropagation"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("GameMode spawned"), GM);
			if (!GM) return;

			GM->BucketClass = ATestBucketSubclass::StaticClass();
			GM->SpawnAssemblyLine();

			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			TestNotNull(TEXT("Director subsystem"), Director);
			if (!Director) return;
			TestEqual(TEXT("Director.BucketClass matches GameMode's"),
				Director->BucketClass.Get(), GM->BucketClass.Get());
		});
	});

	Describe("SpawnFloor (Story 20)", [this]()
	{
		auto CountFloorActors = [](UWorld* World) -> int32
		{
			int32 N = 0;
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				if (It->Tags.Contains(TEXT("AssemblyLineFloor"))) ++N;
			}
			return N;
		};

		It("does NOT spawn a floor actor when FloorMesh is unset", [this, CountFloorActors]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_FloorMissing"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnAssemblyLine();
			GM->SpawnFloor();

			TestEqual(TEXT("zero floor actors when FloorMesh is unset"),
				CountFloorActors(TW.World), 0);
		});

		It("spawns FloorTilesX × FloorTilesY tile actors, each with the assigned "
		   "mesh and per-tile FloorScale, when FloorMesh is set", [this, CountFloorActors]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_FloorAssigned"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			// Real engine mesh — has non-zero bounds, so SpawnFloor's tile-sizing
			// math (mesh extent × scale) actually works.
			UStaticMesh* TestMesh = LoadObject<UStaticMesh>(
				nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			TestNotNull(TEXT("loaded engine cube"), TestMesh);
			if (!TestMesh) return;
			GM->FloorMesh = TSoftObjectPtr<UStaticMesh>(TestMesh);
			GM->FloorScale = FVector(1.f, 1.f, 1.f);
			GM->FloorTilesX = 4;
			GM->FloorTilesY = 3;

			GM->SpawnAssemblyLine();
			GM->SpawnFloor();

			TestEqual(TEXT("FloorTilesX × FloorTilesY tile actors"),
				CountFloorActors(TW.World), 4 * 3);

			AStaticMeshActor* AnyTile = nullptr;
			for (TActorIterator<AStaticMeshActor> It(TW.World); It; ++It)
			{
				if (It->Tags.Contains(TEXT("AssemblyLineFloor"))) { AnyTile = *It; break; }
			}
			if (!AnyTile) return;

			TestEqual(TEXT("tile scale matches per-tile FloorScale"),
				AnyTile->GetActorScale3D(), GM->FloorScale);
			if (UStaticMeshComponent* Smc = AnyTile->GetStaticMeshComponent())
			{
				TestTrue(TEXT("tile mesh is the assigned FloorMesh"),
					Smc->GetStaticMesh() == TestMesh);
			}
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
