#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AgentChatSubsystem.h"
#include "AgentPromptLibrary.h"
#include "AssemblyLineDirector.h"
#include "AssemblyLineFeedback.h"
#include "AssemblyLineGameMode.h"
#include "AssemblyLineTypes.h"
#include "Bucket.h"
#include "CinematicCameraDirector.h"
#include "DAG/AssemblyLineDAG.h"
#include "Engine/GameInstance.h"
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
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

		// Story 35 lifted the AC32b.9 duplicate-kind rejection. The
		// "rejects duplicate kinds" test is gone; multi-instance acceptance
		// is covered by the new "Multi-instance per Kind" Describe block
		// further down (Story 35).

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
	Describe("SpawnLineFromSpec multi-instance per Kind (Story 35)", [this]()
	{
		// Builder for the operator's exact 5-stage mission shape:
		// generate → filter (evens) → sort desc → check → filter (top 2).
		// Two Filters in one spec — Story 32b would have rejected this;
		// Story 35 accepts it.
		auto FiveStationSpec = []() -> TArray<FStationNode>
		{
			const FNodeRef Gen {EStationType::Generator, 0};
			const FNodeRef Flt0{EStationType::Filter,    0};
			const FNodeRef Srt {EStationType::Sorter,    0};
			const FNodeRef Chk {EStationType::Checker,   0};
			const FNodeRef Flt1{EStationType::Filter,    1};
			return {
				FStationNode{Gen,  TEXT("generate 20"),         {}},
				FStationNode{Flt0, TEXT("keep evens"),       {Gen}},
				FStationNode{Srt,  TEXT("sort descending"),  {Flt0}},
				FStationNode{Chk,  TEXT("verify"),           {Srt}},
				FStationNode{Flt1, TEXT("take top 2"),       {Chk}},
			};
		};

		It("accepts a 5-node spec with two Filters and spawns 5 stations + 5 workers", [this, FiveStationSpec]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_S35_FiveStation"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			const bool bOk = GM->SpawnLineFromSpec(FiveStationSpec());
			TestTrue(TEXT("SpawnLineFromSpec accepts the 5-node multi-instance spec"), bOk);

			TestEqual(TEXT("5 stations spawned"), CountAllStations(TW.World), 5);

			int32 WorkerCount = 0;
			for (TActorIterator<AWorkerRobot> It(TW.World); It; ++It) { ++WorkerCount; }
			TestEqual(TEXT("5 workers spawned (one per station instance)"), WorkerCount, 5);

			// Two Filters specifically — verify by class.
			TestEqual(TEXT("2 AFilterStation actors"),
				CountStationsByClass(TW.World, AFilterStation::StaticClass()), 2);
		});

		It("each spawned station's NodeRef matches its spec node's NodeRef", [this, FiveStationSpec]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_S35_NodeRefMatch"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnLineFromSpec(FiveStationSpec());

			AStation* F0 = Director->GetStationByNodeRef(FNodeRef{EStationType::Filter, 0});
			AStation* F1 = Director->GetStationByNodeRef(FNodeRef{EStationType::Filter, 1});
			TestNotNull(TEXT("Filter/0 registered"), F0);
			TestNotNull(TEXT("Filter/1 registered"), F1);
			if (F0 && F1)
			{
				TestNotEqual(TEXT("Filter/0 and Filter/1 are distinct actors"), F0, F1);
				TestEqual(TEXT("Filter/0's CurrentRule is 'keep evens'"),
					F0->CurrentRule, FString(TEXT("keep evens")));
				TestEqual(TEXT("Filter/1's CurrentRule is 'take top 2'"),
					F1->CurrentRule, FString(TEXT("take top 2")));
			}
		});

		It("GetStationOfType(Filter) returns the Filter/0 instance (backward-compat shim)",
		   [this, FiveStationSpec]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_S35_BackwardCompat"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnLineFromSpec(FiveStationSpec());

			AStation* Shim = Director->GetStationOfType(EStationType::Filter);
			AStation* F0   = Director->GetStationByNodeRef(FNodeRef{EStationType::Filter, 0});
			TestEqual(TEXT("shim returns Instance 0 (the 'keep evens' Filter)"), Shim, F0);
			TestEqual(TEXT("shim returns the 'keep evens' Filter, NOT 'take top 2'"),
				Shim ? Shim->CurrentRule : FString(),
				FString(TEXT("keep evens")));
		});
	});

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

	Describe("ClearExistingLine (Story 34 — re-missioning teardown)", [this]()
	{
		// Build a fully-populated line for the tests in this Describe.
		auto SpawnFullLine = [](UWorld* World, AAssemblyLineGameMode* GM)
		{
			GM->SpawnOrchestrator();
			GM->SpawnLineFromSpec(LegacyFourStationSpec());
			GM->SpawnCinematicDirector();
			GM->SpawnFeedback();
		};

		auto CountActorsByPredicate = [](UWorld* World,
			TFunctionRef<bool(AActor*)> Pred) -> int32
		{
			int32 N = 0;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (IsValid(*It) && Pred(*It)) ++N;
			}
			return N;
		};

		It("destroys all non-Orchestrator stations", [this, SpawnFullLine]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_Clear_Stations"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			SpawnFullLine(TW.World, GM);

			TestEqual(TEXT("4 production stations + 1 orchestrator pre-clear"),
				CountAllStations(TW.World), 5);

			GM->ClearExistingLine();

			// Only the Orchestrator survives.
			int32 ProductionStations = 0;
			int32 OrchStations = 0;
			for (TActorIterator<AStation> It(TW.World); It; ++It)
			{
				if (!IsValid(*It)) continue;
				if (It->IsA(AOrchestratorStation::StaticClass())) ++OrchStations;
				else ++ProductionStations;
			}
			TestEqual(TEXT("zero production stations remain"), ProductionStations, 0);
			TestEqual(TEXT("Orchestrator station survives"), OrchStations, 1);
		});

		It("destroys all worker robots", [this, SpawnFullLine]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_Clear_Workers"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			SpawnFullLine(TW.World, GM);

			int32 WorkersBefore = 0;
			for (TActorIterator<AWorkerRobot> It(TW.World); It; ++It) { ++WorkersBefore; }
			TestEqual(TEXT("4 workers pre-clear"), WorkersBefore, 4);

			GM->ClearExistingLine();

			int32 WorkersAfter = 0;
			for (TActorIterator<AWorkerRobot> It(TW.World); It; ++It)
			{
				if (IsValid(*It)) ++WorkersAfter;
			}
			TestEqual(TEXT("zero workers post-clear"), WorkersAfter, 0);
		});

		It("destroys every bucket in the world (in-flight or idle)", [this, SpawnFullLine]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_Clear_Buckets"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			SpawnFullLine(TW.World, GM);

			// Spawn three "in-flight" buckets that the previous mission's
			// workers might have been carrying.
			for (int32 i = 0; i < 3; ++i)
			{
				ABucket* B = TW.World->SpawnActor<ABucket>(
					ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
				B->Contents = {i};
			}

			int32 BucketsBefore = 0;
			for (TActorIterator<ABucket> It(TW.World); It; ++It) { ++BucketsBefore; }
			TestEqual(TEXT("3 buckets pre-clear"), BucketsBefore, 3);

			GM->ClearExistingLine();

			int32 BucketsAfter = 0;
			for (TActorIterator<ABucket> It(TW.World); It; ++It)
			{
				if (IsValid(*It)) ++BucketsAfter;
			}
			TestEqual(TEXT("zero buckets post-clear"), BucketsAfter, 0);
		});

		It("destroys the cinematic camera director", [this, SpawnFullLine]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_Clear_Cinematic"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			SpawnFullLine(TW.World, GM);

			int32 CinBefore = 0;
			for (TActorIterator<ACinematicCameraDirector> It(TW.World); It; ++It) { ++CinBefore; }
			TestEqual(TEXT("1 cinematic pre-clear"), CinBefore, 1);

			GM->ClearExistingLine();

			int32 CinAfter = 0;
			for (TActorIterator<ACinematicCameraDirector> It(TW.World); It; ++It)
			{
				if (IsValid(*It)) ++CinAfter;
			}
			TestEqual(TEXT("zero cinematics post-clear"), CinAfter, 0);
		});

		It("destroys the feedback actor", [this, SpawnFullLine]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_Clear_Feedback"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			SpawnFullLine(TW.World, GM);

			int32 FbBefore = 0;
			for (TActorIterator<AAssemblyLineFeedback> It(TW.World); It; ++It) { ++FbBefore; }
			TestEqual(TEXT("1 feedback pre-clear"), FbBefore, 1);

			GM->ClearExistingLine();

			int32 FbAfter = 0;
			for (TActorIterator<AAssemblyLineFeedback> It(TW.World); It; ++It)
			{
				if (IsValid(*It)) ++FbAfter;
			}
			TestEqual(TEXT("zero feedbacks post-clear"), FbAfter, 0);
		});

		It("preserves AssemblyLineFloor-tagged static mesh actors", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_Clear_FloorPreserved"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnOrchestrator();

			// Spawn a tagged "floor" tile by hand (avoids the FloorMesh
			// dependency). Tag is what GameMode uses to identify floor.
			AStaticMeshActor* Tile = TW.World->SpawnActor<AStaticMeshActor>(
				AStaticMeshActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			Tile->Tags.AddUnique(TEXT("AssemblyLineFloor"));

			GM->ClearExistingLine();

			TestTrue(TEXT("floor tile actor still valid post-clear"),
				IsValid(Tile));
		});

		It("is a no-op on a fresh boot world (only Orchestrator + no other line state)", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_Clear_NoOp"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnOrchestrator();

			TestEqual(TEXT("only Orchestrator pre-clear"),
				CountAllStations(TW.World), 1);

			// Should not crash, should not destroy the Orchestrator.
			GM->ClearExistingLine();

			TestEqual(TEXT("Orchestrator still present"),
				CountStationsByClass(TW.World, AOrchestratorStation::StaticClass()), 1);
			TestEqual(TEXT("no other stations appeared/disappeared"),
				CountAllStations(TW.World), 1);
		});

		It("wipes stale Saved/Agents/<Kind>.md for all four production kinds", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_Clear_WipesSavedAgents"));

			// Pre-seed Saved/Agents/ with four files as if a prior mission
			// had written them.
			const FString SavedAgentsDir = FPaths::ProjectSavedDir() / TEXT("Agents");
			IFileManager::Get().MakeDirectory(*SavedAgentsDir, /*Tree=*/true);
			const TArray<FString> Files = {
				SavedAgentsDir / TEXT("Generator.md"),
				SavedAgentsDir / TEXT("Filter.md"),
				SavedAgentsDir / TEXT("Sorter.md"),
				SavedAgentsDir / TEXT("Checker.md"),
			};
			for (const FString& Path : Files)
			{
				FFileHelper::SaveStringToFile(TEXT("# stale\n"), *Path);
				TestTrue(TEXT("seeded file exists"),
					IFileManager::Get().FileExists(*Path));
			}

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnOrchestrator();
			GM->ClearExistingLine();

			for (const FString& Path : Files)
			{
				TestFalse(TEXT("stale Saved/Agents/ file deleted"),
					IFileManager::Get().FileExists(*Path));
			}

			AgentPromptLibrary::InvalidateCache();
		});
	});

	Describe("HandleDAGProposed re-missioning (Story 34)", [this]()
	{
		auto SavedAgentMdPath = [](EStationType Kind) -> FString
		{
			const TCHAR* Filename = TEXT("");
			switch (Kind)
			{
			case EStationType::Generator: Filename = TEXT("Generator.md"); break;
			case EStationType::Filter:    Filename = TEXT("Filter.md");    break;
			case EStationType::Sorter:    Filename = TEXT("Sorter.md");    break;
			case EStationType::Checker:   Filename = TEXT("Checker.md");   break;
			default: break;
			}
			return FPaths::ProjectSavedDir() / TEXT("Agents") / Filename;
		};

		auto CleanupSavedAgents = [SavedAgentMdPath]()
		{
			for (EStationType K : {EStationType::Generator, EStationType::Filter,
				EStationType::Sorter, EStationType::Checker})
			{
				IFileManager::Get().Delete(*SavedAgentMdPath(K));
			}
			AgentPromptLibrary::InvalidateCache();
		};

		It("second HandleDAGProposed leaves only the second mission's actors "
		   "(reproduces the operator-observed duplicate-bucket bug)", [this, CleanupSavedAgents]()
		{
			CleanupSavedAgents();

			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_ReMission_Counts"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnOrchestrator();

			// Mission A: 4-station legacy line.
			const TMap<EStationType, FString> NoPrompts;
			GM->HandleDAGProposed(LegacyFourStationSpec(), NoPrompts);

			TestEqual(TEXT("after mission A: 4 production + 1 orchestrator stations"),
				CountAllStations(TW.World), 5);

			// Mission B: 3-station line (no Sorter).
			const FNodeRef Gen{EStationType::Generator, 0};
			const FNodeRef Flt{EStationType::Filter,    0};
			const FNodeRef Chk{EStationType::Checker,   0};
			const TArray<FStationNode> ThreeStationSpec = {
				FStationNode{Gen, FString(),       {}},
				FStationNode{Flt, FString(),    {Gen}},
				FStationNode{Chk, FString(),    {Flt}},
			};
			GM->HandleDAGProposed(ThreeStationSpec, NoPrompts);

			// Bug under repair: without the clear, this would be 7 + 1 = 8.
			// With the clear, it should be exactly 3 + 1 = 4.
			TestEqual(TEXT("after mission B: only B's 3 + Orchestrator = 4 stations"),
				CountAllStations(TW.World), 4);
			TestNull(TEXT("Sorter from mission A is gone"),
				[TW]() {
					for (TActorIterator<ASorterStation> It(TW.World); It; ++It)
					{
						if (IsValid(*It)) return (AStation*)*It;
					}
					return (AStation*)nullptr;
				}());

			// Worker count also exact, not doubled.
			int32 WorkerCount = 0;
			for (TActorIterator<AWorkerRobot> It(TW.World); It; ++It)
			{
				if (IsValid(*It)) ++WorkerCount;
			}
			TestEqual(TEXT("3 workers (one per mission B station)"), WorkerCount, 3);

			CleanupSavedAgents();
		});

		It("preserves Orchestrator registration in Director across re-mission", [this, CleanupSavedAgents]()
		{
			CleanupSavedAgents();

			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_ReMission_OrchPreserved"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnOrchestrator();
			AStation* OrchBefore = Director->GetStationOfType(EStationType::Orchestrator);
			TestNotNull(TEXT("Orchestrator registered after SpawnOrchestrator"), OrchBefore);

			const TMap<EStationType, FString> NoPrompts;
			GM->HandleDAGProposed(LegacyFourStationSpec(), NoPrompts);
			GM->HandleDAGProposed(LegacyFourStationSpec(), NoPrompts);

			AStation* OrchAfter = Director->GetStationOfType(EStationType::Orchestrator);
			TestNotNull(TEXT("Orchestrator still registered after re-mission"), OrchAfter);
			TestEqual(TEXT("same Orchestrator instance preserved"), OrchAfter, OrchBefore);

			CleanupSavedAgents();
		});

		It("in-flight bucket from the prior mission is destroyed by re-mission", [this, CleanupSavedAgents]()
		{
			CleanupSavedAgents();

			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_ReMission_InFlightBucket"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnOrchestrator();
			const TMap<EStationType, FString> NoPrompts;
			GM->HandleDAGProposed(LegacyFourStationSpec(), NoPrompts);

			// Simulate an in-flight bucket as if a worker was carrying it.
			ABucket* InFlight = TW.World->SpawnActor<ABucket>(
				ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			InFlight->Contents = {42};
			TestTrue(TEXT("in-flight bucket exists pre-re-mission"), IsValid(InFlight));

			// Re-mission with the same spec.
			GM->HandleDAGProposed(LegacyFourStationSpec(), NoPrompts);

			TestFalse(TEXT("in-flight bucket destroyed by re-mission"),
				IsValid(InFlight));

			CleanupSavedAgents();
		});

		It("subsequent station construction reads the new mission's "
		   "Saved/Agents/<Kind>.md, not stale prior-mission content", [this, CleanupSavedAgents, SavedAgentMdPath]()
		{
			CleanupSavedAgents();

			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_ReMission_FreshPrompts"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnOrchestrator();

			// Mission A: writes Filter.md with "MISSION_A_ROLE".
			TMap<EStationType, FString> PromptsA;
			PromptsA.Add(EStationType::Filter, TEXT("MISSION_A_ROLE"));
			GM->HandleDAGProposed(LegacyFourStationSpec(), PromptsA);

			FString FltContentA;
			FFileHelper::LoadFileToString(FltContentA, *SavedAgentMdPath(EStationType::Filter));
			TestTrue(TEXT("after mission A: Filter.md contains A's role"),
				FltContentA.Contains(TEXT("MISSION_A_ROLE")));

			// Mission B: writes Filter.md with "MISSION_B_ROLE".
			TMap<EStationType, FString> PromptsB;
			PromptsB.Add(EStationType::Filter, TEXT("MISSION_B_ROLE"));
			GM->HandleDAGProposed(LegacyFourStationSpec(), PromptsB);

			FString FltContentB;
			FFileHelper::LoadFileToString(FltContentB, *SavedAgentMdPath(EStationType::Filter));
			TestTrue(TEXT("after mission B: Filter.md contains B's role"),
				FltContentB.Contains(TEXT("MISSION_B_ROLE")));
			TestFalse(TEXT("after mission B: Filter.md no longer contains A's role"),
				FltContentB.Contains(TEXT("MISSION_A_ROLE")));

			CleanupSavedAgents();
		});
	});

	Describe("HandleDAGProposed → WriteOrchestratorAuthoredPrompts (Story 33b)", [this]()
	{
		auto SavedAgentMdPath = [](EStationType Kind) -> FString
		{
			const TCHAR* Filename = TEXT("");
			switch (Kind)
			{
			case EStationType::Generator:    Filename = TEXT("Generator.md");    break;
			case EStationType::Filter:       Filename = TEXT("Filter.md");       break;
			case EStationType::Sorter:       Filename = TEXT("Sorter.md");       break;
			case EStationType::Checker:      Filename = TEXT("Checker.md");      break;
			case EStationType::Orchestrator: Filename = TEXT("Orchestrator.md"); break;
			}
			return FPaths::ProjectSavedDir() / TEXT("Agents") / Filename;
		};

		auto CleanupSavedAgents = [SavedAgentMdPath]()
		{
			for (EStationType K : {EStationType::Generator, EStationType::Filter,
				EStationType::Sorter, EStationType::Checker, EStationType::Orchestrator})
			{
				IFileManager::Get().Delete(*SavedAgentMdPath(K));
			}
			AgentPromptLibrary::InvalidateCache();
		};

		It("writes Saved/Agents/<Kind>.md for every entry in PromptsByKind, with "
		   "the Orchestrator-authored Role embedded and the static ProcessBucketPrompt "
		   "preserved verbatim", [this, SavedAgentMdPath, CleanupSavedAgents]()
		{
			CleanupSavedAgents();

			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_WriteAuthoredPrompts"));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			const FNodeRef Gen{EStationType::Generator, 0};
			const FNodeRef Flt{EStationType::Filter,    0};
			const TArray<FStationNode> Spec = {
				FStationNode{Gen, TEXT("crank-out-numbers"),    {}},
				FStationNode{Flt, TEXT("keep-only-the-primes"), {Gen}},
			};
			TMap<EStationType, FString> Prompts;
			Prompts.Add(EStationType::Generator,
				TEXT("You are the source of fresh integer batches for this mission."));
			Prompts.Add(EStationType::Filter,
				TEXT("You sift the wheat from the chaff for this mission."));

			GM->WriteOrchestratorAuthoredPrompts(Spec, Prompts);

			// File presence.
			TestTrue(TEXT("Saved/Agents/Generator.md exists"),
				IFileManager::Get().FileExists(*SavedAgentMdPath(EStationType::Generator)));
			TestTrue(TEXT("Saved/Agents/Filter.md exists"),
				IFileManager::Get().FileExists(*SavedAgentMdPath(EStationType::Filter)));

			// Generator file contents — Role authored, Rule from spec, ProcessBucketPrompt
			// from the static template (so JSON-result parsing remains valid).
			FString GenContent;
			FFileHelper::LoadFileToString(GenContent, *SavedAgentMdPath(EStationType::Generator));
			TestTrue(TEXT("Generator.md contains Orchestrator-authored Role"),
				GenContent.Contains(TEXT("source of fresh integer batches")));
			TestTrue(TEXT("Generator.md contains DefaultRule from the spec"),
				GenContent.Contains(TEXT("crank-out-numbers")));
			TestTrue(TEXT("Generator.md preserves the static ProcessBucketPrompt JSON contract"),
				GenContent.Contains(TEXT("{\"result\":[<integers>]}")));

			// Filter file — same structure.
			FString FltContent;
			FFileHelper::LoadFileToString(FltContent, *SavedAgentMdPath(EStationType::Filter));
			TestTrue(TEXT("Filter.md contains Orchestrator-authored Role"),
				FltContent.Contains(TEXT("wheat from the chaff")));
			TestTrue(TEXT("Filter.md contains DefaultRule from the spec"),
				FltContent.Contains(TEXT("keep-only-the-primes")));
			TestTrue(TEXT("Filter.md preserves the static ProcessBucketPrompt {{rule}} placeholder"),
				FltContent.Contains(TEXT("{{rule}}")));

			CleanupSavedAgents();
		});

		It("writing a Checker prompt also preserves the static DerivedRuleTemplate "
		   "section so the Checker can still compose ancestor rules", [this, SavedAgentMdPath, CleanupSavedAgents]()
		{
			CleanupSavedAgents();

			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_WriteAuthoredPrompts_Checker"));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			const TArray<FStationNode> Spec = {
				FStationNode{FNodeRef{EStationType::Checker, 0}, TEXT("verify the bucket"), {}},
			};
			TMap<EStationType, FString> Prompts;
			Prompts.Add(EStationType::Checker,
				TEXT("You are the final word on whether a bucket passes."));

			GM->WriteOrchestratorAuthoredPrompts(Spec, Prompts);

			FString ChkContent;
			FFileHelper::LoadFileToString(ChkContent, *SavedAgentMdPath(EStationType::Checker));
			TestTrue(TEXT("Checker.md contains the authored Role"),
				ChkContent.Contains(TEXT("final word")));
			TestTrue(TEXT("Checker.md preserves DerivedRuleTemplate placeholders"),
				ChkContent.Contains(TEXT("{{generator_rule}}")) ||
				ChkContent.Contains(TEXT("DerivedRuleTemplate")));

			CleanupSavedAgents();
		});
	});

	Describe("SendDefaultMission (Story 33a — file-driven kickoff)", [this]()
	{
		It("reads the Mission section and pushes it through the chat subsystem "
		   "as a user message addressed to the Orchestrator", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_SendDefaultMission"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("GameMode spawned"), GM);
			if (!GM) return;

			// Tests run without a real UGameInstance subsystem collection, so
			// inject a transient chat subsystem and assert against its history.
			UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
			UAgentChatSubsystem* Chat = NewObject<UAgentChatSubsystem>(GI);
			GM->SetChatSubsystemForTesting(Chat);

			GM->SendDefaultMission();

			const TArray<FAgentChatMessage>& Hist = Chat->GetHistory(EStationType::Orchestrator);
			TestTrue(TEXT("Orchestrator history has the Mission as a user message"),
				Hist.ContainsByPredicate([](const FAgentChatMessage& M)
				{
					return M.Role == TEXT("user") && !M.Text.IsEmpty()
						&& M.Text.Contains(TEXT("filter"), ESearchCase::IgnoreCase)
						&& M.Text.Contains(TEXT("prime"), ESearchCase::IgnoreCase);
				}));

			// No other agent should have received the message.
			TestEqual(TEXT("Filter history untouched"),
				Chat->GetHistory(EStationType::Filter).Num(), 0);
		});

		It("is a no-op when the chat subsystem is unavailable (logs Warning, no crash)", [this]()
		{
			AddExpectedError(TEXT("chat subsystem"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);

			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_SendDefaultMission_NoChat"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			// No chat injected; no GameInstance subsystem available either.
			GM->SendDefaultMission();
			// Assertion is the AddExpectedError above + no crash.
		});
	});

	Describe("SpawnCinematicDirector (Story 36 — single wide overview, follow camera handles closeups)", [this]()
	{
		It("4-station line yields exactly ONE wide-overview shot (closeups handled by FollowCamera)", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_CinematicWideOnly_4"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnLineFromSpec(LegacyFourStationSpec());
			GM->SpawnCinematicDirector();

			int32 CinDirectorCount = 0;
			int32 ShotsConfigured = 0;
			ACinematicCameraDirector* Cin = nullptr;
			for (TActorIterator<ACinematicCameraDirector> It(TW.World); It; ++It)
			{
				++CinDirectorCount;
				ShotsConfigured = It->Shots.Num();
				Cin = *It;
			}

			TestEqual(TEXT("exactly one CinematicCameraDirector"), CinDirectorCount, 1);
			TestEqual(TEXT("exactly 1 shot (the wide overview)"), ShotsConfigured, 1);
			TestNotNull(TEXT("FollowCamera spawned"), Cin ? Cin->GetFollowCamera() : nullptr);
		});

		It("authors a non-empty DefaultFollowSequence (Story 36 zoom dance)", [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_DefaultSequence"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			GM->SpawnLineFromSpec(LegacyFourStationSpec());
			GM->SpawnCinematicDirector();

			ACinematicCameraDirector* Cin = nullptr;
			for (TActorIterator<ACinematicCameraDirector> It(TW.World); It; ++It) { Cin = *It; break; }
			TestNotNull(TEXT("cinematic spawned"), Cin);
			if (!Cin) return;

			TestTrue(TEXT("DefaultFollowSequence has keyframes"),
				Cin->DefaultFollowSequence.Keyframes.Num() >= 2);
		});

		It("5-station mission with two Filters still yields exactly 1 wide shot (multi-instance ignored at shot level)",
		   [this]()
		{
			FScopedTestWorld TW(TEXT("AssemblyLineGameModeSpec_CinematicWideOnly_5"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!GM) return;

			const FNodeRef Gen {EStationType::Generator, 0};
			const FNodeRef Flt0{EStationType::Filter,    0};
			const FNodeRef Srt {EStationType::Sorter,    0};
			const FNodeRef Chk {EStationType::Checker,   0};
			const FNodeRef Flt1{EStationType::Filter,    1};
			GM->SpawnLineFromSpec({
				FStationNode{Gen,  FString(),         {}},
				FStationNode{Flt0, FString(),      {Gen}},
				FStationNode{Srt,  FString(),     {Flt0}},
				FStationNode{Chk,  FString(),      {Srt}},
				FStationNode{Flt1, FString(),      {Chk}},
			});
			GM->SpawnCinematicDirector();

			int32 ShotsConfigured = 0;
			for (TActorIterator<ACinematicCameraDirector> It(TW.World); It; ++It)
			{
				ShotsConfigured = It->Shots.Num();
				break;
			}
			TestEqual(TEXT("still exactly 1 shot regardless of station count"), ShotsConfigured, 1);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
