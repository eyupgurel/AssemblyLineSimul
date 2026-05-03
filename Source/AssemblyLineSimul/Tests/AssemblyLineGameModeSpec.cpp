#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyLineDirector.h"
#include "AssemblyLineGameMode.h"
#include "CinematicCameraDirector.h"
#include "DAG/AssemblyLineDAG.h"
#include "Station.h"
#include "StationSubclasses.h"
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

	// Story 32b — the legacy 4-station linear chain, now expressed as a DAG
	// spec instead of a hardcoded SpawnAssemblyLine call. Used by the existing
	// tests that need a fully-populated line; the boot-only path uses
	// SpawnOrchestrator instead.
	static TArray<FStationNode> LegacyFourStationSpec()
	{
		const FNodeRef Gen{EStationType::Generator, 0};
		const FNodeRef Flt{EStationType::Filter,    0};
		const FNodeRef Srt{EStationType::Sorter,    0};
		const FNodeRef Chk{EStationType::Checker,   0};
		return {
			FStationNode{Gen, FString(),       {}},
			FStationNode{Flt, FString(),    {Gen}},
			FStationNode{Srt, FString(),    {Flt}},
			FStationNode{Chk, FString(),    {Srt}},
		};
	}

	static int32 CountStationsByClass(UWorld* World, UClass* Cls)
	{
		int32 N = 0;
		for (TActorIterator<AStation> It(World); It; ++It)
		{
			if (IsValid(*It) && It->IsA(Cls)) ++N;
		}
		return N;
	}

	static int32 CountAllStations(UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<AStation> It(World); It; ++It)
		{
			if (IsValid(*It)) ++N;
		}
		return N;
	}
}

DEFINE_SPEC(FAssemblyLineGameModeSpec,
	"AssemblyLineSimul.AssemblyLineGameMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FAssemblyLineGameModeSpec::Define()
{
	using namespace AssemblyLineGameModeTests;

	Describe("SpawnOrchestrator (Story 32b boot path)", [this]()
	{
		It("spawns exactly one AOrchestratorStation and zero other station kinds", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_OrchestratorOnly"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("GameMode spawned"), GM);
			if (!GM) return;

			GM->SpawnOrchestrator();

			TestEqual(TEXT("exactly one AOrchestratorStation"),
				CountStationsByClass(TW.World, AOrchestratorStation::StaticClass()), 1);
			TestEqual(TEXT("zero other AStation actors"),
				CountAllStations(TW.World) - 1, 0);

			// Worker count must also be zero — Orchestrator is chat-only.
			int32 WorkerCount = 0;
			for (TActorIterator<AWorkerRobot> It(TW.World); It; ++It) { ++WorkerCount; }
			TestEqual(TEXT("zero workers spawned at boot"), WorkerCount, 0);
		});

		It("registers the spawned Orchestrator with the Director so chat lookup works", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_OrchestratorRegistered"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnOrchestrator();

			AStation* Found = Director->GetStationOfType(EStationType::Orchestrator);
			TestNotNull(TEXT("Orchestrator station registered with Director"), Found);
			if (Found)
			{
				TestTrue(TEXT("registered station is an AOrchestratorStation"),
					Found->IsA(AOrchestratorStation::StaticClass()));
			}
		});
	});

	Describe("SpawnLineFromSpec (Story 32b mission path)", [this]()
	{
		It("spawns one station + one worker per node and applies per-node rules", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_SpawnFromSpec_Linear"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			const FNodeRef Gen{EStationType::Generator, 0};
			const FNodeRef Flt{EStationType::Filter,    0};
			const FNodeRef Srt{EStationType::Sorter,    0};
			const FNodeRef Chk{EStationType::Checker,   0};
			const TArray<FStationNode> Spec = {
				FStationNode{Gen, TEXT("gen-rule"),       {}},
				FStationNode{Flt, TEXT("flt-rule"),    {Gen}},
				FStationNode{Srt, TEXT("srt-rule"),    {Flt}},
				FStationNode{Chk, TEXT("chk-rule"),    {Srt}},
			};

			const bool bOk = GM->SpawnLineFromSpec(Spec);
			TestTrue(TEXT("SpawnLineFromSpec returned true on a valid spec"), bOk);

			TestEqual(TEXT("4 stations spawned"), CountAllStations(TW.World), 4);

			int32 WorkerCount = 0;
			for (TActorIterator<AWorkerRobot> It(TW.World); It; ++It) { ++WorkerCount; }
			TestEqual(TEXT("4 workers spawned"), WorkerCount, 4);

			AStation* GenStation = Director->GetStationOfType(EStationType::Generator);
			AStation* FltStation = Director->GetStationOfType(EStationType::Filter);
			AStation* SrtStation = Director->GetStationOfType(EStationType::Sorter);
			AStation* ChkStation = Director->GetStationOfType(EStationType::Checker);
			TestNotNull(TEXT("Generator registered"), GenStation);
			TestNotNull(TEXT("Filter registered"),    FltStation);
			TestNotNull(TEXT("Sorter registered"),    SrtStation);
			TestNotNull(TEXT("Checker registered"),   ChkStation);

			if (GenStation) TestEqual(TEXT("Gen rule"), GenStation->CurrentRule, FString(TEXT("gen-rule")));
			if (FltStation) TestEqual(TEXT("Flt rule"), FltStation->CurrentRule, FString(TEXT("flt-rule")));
			if (SrtStation) TestEqual(TEXT("Srt rule"), SrtStation->CurrentRule, FString(TEXT("srt-rule")));
			if (ChkStation) TestEqual(TEXT("Chk rule"), ChkStation->CurrentRule, FString(TEXT("chk-rule")));
		});

		It("spawns the correct AStation subclass for each node Kind", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_SpawnFromSpec_Subclasses"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnLineFromSpec(LegacyFourStationSpec());

			TestEqual(TEXT("1 Generator"),
				CountStationsByClass(TW.World, AGeneratorStation::StaticClass()), 1);
			TestEqual(TEXT("1 Filter"),
				CountStationsByClass(TW.World, AFilterStation::StaticClass()), 1);
			TestEqual(TEXT("1 Sorter"),
				CountStationsByClass(TW.World, ASorterStation::StaticClass()), 1);
			TestEqual(TEXT("1 Checker"),
				CountStationsByClass(TW.World, ACheckerStation::StaticClass()), 1);
		});

		It("rejects a spec containing duplicate kinds (v1 single-instance constraint, AC32b.9)", [this]()
		{
			AddExpectedError(TEXT("duplicate"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);

			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_SpawnFromSpec_DupKind"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			// Two Filters in the same spec — disallowed in v1 because chat
			// routing keys on EStationType (one Filter per kind).
			const FNodeRef Gen{EStationType::Generator, 0};
			const FNodeRef Flt0{EStationType::Filter,   0};
			const FNodeRef Flt1{EStationType::Filter,   1};
			const TArray<FStationNode> DupSpec = {
				FStationNode{Gen,  FString(),       {}},
				FStationNode{Flt0, FString(),    {Gen}},
				FStationNode{Flt1, FString(),    {Gen}},
			};

			const bool bOk = GM->SpawnLineFromSpec(DupSpec);
			TestFalse(TEXT("SpawnLineFromSpec returned false on duplicate-kind spec"), bOk);
			TestEqual(TEXT("zero stations spawned"), CountAllStations(TW.World), 0);
		});

		It("returns false and leaves world untouched on a cyclic spec", [this]()
		{
			// Two log lines mention "cycle": one from FAssemblyLineDAG::BuildFromDAG
			// (Kahn drain), one from SpawnLineFromSpec's wrapper that re-logs the
			// validation failure with category context.
			AddExpectedError(TEXT("cycle"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/2);

			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_SpawnFromSpec_Cycle"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			// Gen -> Flt -> Gen (cycle).
			const FNodeRef Gen{EStationType::Generator, 0};
			const FNodeRef Flt{EStationType::Filter,    0};
			const TArray<FStationNode> CyclicSpec = {
				FStationNode{Gen, FString(), {Flt}},
				FStationNode{Flt, FString(), {Gen}},
			};

			const bool bOk = GM->SpawnLineFromSpec(CyclicSpec);
			TestFalse(TEXT("SpawnLineFromSpec returned false on cycle"), bOk);
			TestEqual(TEXT("zero stations spawned"), CountAllStations(TW.World), 0);
		});
	});

	// Story 32b — these tests previously used SpawnAssemblyLine to set up a
	// 4-station line; they now use SpawnLineFromSpec(LegacyFourStationSpec).
	// Same coverage, new entry point.
	Describe("SpawnLineFromSpec — propagation (formerly SpawnAssemblyLine tests)", [this]()
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

			GM->SpawnLineFromSpec(LegacyFourStationSpec());

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
			GM->SpawnLineFromSpec(LegacyFourStationSpec());

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

			UStaticMesh* TestMesh = LoadObject<UStaticMesh>(
				nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			TestNotNull(TEXT("loaded engine cube"), TestMesh);
			if (!TestMesh) return;
			GM->FloorMesh = TSoftObjectPtr<UStaticMesh>(TestMesh);
			GM->FloorScale = FVector(1.f, 1.f, 1.f);
			GM->FloorTilesX = 4;
			GM->FloorTilesY = 3;

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

	Describe("SpawnCinematicDirector (Story 32b — shots regen from spawned stations)", [this]()
	{
		It("4-station line yields 1 wide + 4 closeup shots", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_CinematicShots4"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnLineFromSpec(LegacyFourStationSpec());
			GM->SpawnCinematicDirector();

			int32 CinDirectorCount = 0;
			int32 ShotsConfigured = 0;
			for (TActorIterator<ACinematicCameraDirector> It(TW.World); It; ++It)
			{
				++CinDirectorCount;
				ShotsConfigured = It->Shots.Num();
			}

			TestEqual(TEXT("exactly one CinematicCameraDirector"), CinDirectorCount, 1);
			TestEqual(TEXT("5 shots: 1 wide + 4 station closeups"), ShotsConfigured, 5);
		});

		It("2-station line yields 1 wide + 2 closeup shots (proves regen, not hardcoded count)", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_CinematicShots2"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			const FNodeRef Gen{EStationType::Generator, 0};
			const FNodeRef Chk{EStationType::Checker,   0};
			const TArray<FStationNode> TwoNode = {
				FStationNode{Gen, FString(),       {}},
				FStationNode{Chk, FString(),    {Gen}},
			};
			GM->SpawnLineFromSpec(TwoNode);
			GM->SpawnCinematicDirector();

			int32 ShotsConfigured = 0;
			for (TActorIterator<ACinematicCameraDirector> It(TW.World); It; ++It)
			{
				ShotsConfigured = It->Shots.Num();
			}

			TestEqual(TEXT("3 shots: 1 wide + 2 station closeups"), ShotsConfigured, 3);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
