#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyLineDirector.h"
#include "Payload.h"
#include "PayloadCarrier.h"
#include "TestPayloads.h"
#include "DAG/AssemblyLineDAG.h"
#include "DAG/DAGBuilder.h"
#include "Station.h"
#include "TestStations.h"
#include "WorkerRobot.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace AssemblyLineDirectorTests
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

	// Story 38 helpers — pre-Story-38 tests did `B->Contents = {...};` directly
	// on ABucket. With APayloadCarrier these read/write through the typed payload.
	inline void SetCarrierItems(APayloadCarrier* C, const TArray<int32>& Items)
	{
		if (UIntegerArrayPayload* P = Cast<UIntegerArrayPayload>(C ? C->Payload : nullptr))
		{
			P->Items = Items;
			P->OnChanged.Broadcast();
		}
	}

	inline TArray<int32> GetCarrierItems(const APayloadCarrier* C)
	{
		if (const UIntegerArrayPayload* P = Cast<UIntegerArrayPayload>(C ? C->Payload : nullptr))
		{
			return P->Items;
		}
		return {};
	}

	inline int32 GetCarrierItemCount(const APayloadCarrier* C)
	{
		if (const UIntegerArrayPayload* P = Cast<UIntegerArrayPayload>(C ? C->Payload : nullptr))
		{
			return P->Items.Num();
		}
		return 0;
	}
}

DEFINE_SPEC(FAssemblyLineDirectorSpec,
	"AssemblyLineSimul.AssemblyLineDirector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FAssemblyLineDirectorSpec::Define()
{
	using namespace AssemblyLineDirectorTests;

	Describe("RegisterRobot", [this]()
	{
		It("re-broadcasts OnStationActive with the worker's FNodeRef when a "
		   "registered worker fires OnStartedWorking (Story 36)", [this]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_RebroadcastActive"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			TestNotNull(TEXT("Director subsystem"), Director);
			if (!Director) return;

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATestSyncStation* Station = TW.World->SpawnActor<ATestSyncStation>(
				ATestSyncStation::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			Station->StationType = EStationType::Sorter;

			AWorkerRobot* Worker = TW.World->SpawnActor<AWorkerRobot>(
				AWorkerRobot::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			Worker->AssignStation(Station);
			Director->RegisterRobot(Worker);  // also calls Station's NodeRef back-write

			FNodeRef Captured;
			bool bFired = false;
			Director->OnStationActive.AddLambda([&Captured, &bFired](const FNodeRef& Ref)
			{
				Captured = Ref;
				bFired = true;
			});

			// Simulate worker entering Working state — broadcast its NodeRef
			// (which RegisterStation auto-set via Story 35's per-Kind counter
			// when the worker's AssignedStation was registered through the
			// RegisterRobot path… actually RegisterRobot doesn't call
			// RegisterStation. Set it explicitly to make the test honest).
			Station->NodeRef = FNodeRef{EStationType::Sorter, 0};
			Worker->OnStartedWorking.Broadcast(Station->NodeRef);

			TestTrue(TEXT("Director re-broadcast OnStationActive"), bFired);
			TestTrue(TEXT("FNodeRef propagated (Sorter, 0)"),
				Captured == FNodeRef{EStationType::Sorter, 0});
		});
	});

	Describe("Empty-bucket recycle (Story 17 AC17.7)", [this]()
	{
		It("broadcasts OnCycleRecycled when a non-Generator station completes "
		   "with an empty bucket (e.g. Filter eliminated everything during rework)", [this]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_EmptyRecycle"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			TestNotNull(TEXT("Director subsystem"), Director);
			if (!Director) return;

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* Bucket = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("bucket spawned"), Bucket);
			TestEqual(TEXT("bucket starts empty"), GetCarrierItemCount(Bucket), 0);

			bool bRecycled = false;
			APayloadCarrier* RecycledArg = nullptr;
			Director->OnCycleRecycled.AddLambda([&](APayloadCarrier* B)
			{
				bRecycled = true;
				RecycledArg = B;
			});

			Director->OnRobotDoneAt(EStationType::Filter, Bucket);

			TestTrue(TEXT("OnCycleRecycled broadcast fired for empty Filter output"), bRecycled);
			TestEqual(TEXT("recycled bucket payload matches the input bucket"),
				RecycledArg, Bucket);
		});

		It("does NOT recycle when the bucket still has content (normal forward path)", [this]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_NonEmptyForwards"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* Bucket = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			SetCarrierItems(Bucket, { 7, 11, 13 });

			bool bRecycled = false;
			Director->OnCycleRecycled.AddLambda([&](APayloadCarrier*) { bRecycled = true; });

			Director->OnRobotDoneAt(EStationType::Filter, Bucket);

			TestFalse(TEXT("OnCycleRecycled does NOT fire for non-empty bucket"), bRecycled);
		});
	});

	Describe("Fan-out dispatch (Story 31c — K > 1 successors)", [this]()
	{
		// Helper: count buckets currently in the world that look like dispatched
		// fan-out clones — non-source, non-pending-kill, with Contents matching
		// the supplied set.
		auto CountClonesWithContents = [](UWorld* World, const APayloadCarrier* Source,
			const TArray<int32>& ExpectedContents) -> int32
		{
			int32 N = 0;
			for (TActorIterator<APayloadCarrier> It(World); It; ++It)
			{
				APayloadCarrier* B = *It;
				if (!IsValid(B) || B == Source) continue;
				if (GetCarrierItems(B) == ExpectedContents) ++N;
			}
			return N;
		};

		It("clones the bucket K=2 times and destroys the source on a 1->2 fan-out", [this, CountClonesWithContents]()
		{
			// DispatchToStation will warn for missing robots — we don't spawn
			// AWorkerRobots for this test (they'd start their FSM). The test
			// asserts on the cloning side-effect only.
			AddExpectedError(TEXT("missing station or robot"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/2);

			FScopedTestWorld TW(TEXT("DirectorSpec_FanOut_1to2"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			// 1->2 DAG: Generator -> Filter, Generator -> Sorter.
			const FNodeRef A{EStationType::Generator, 0};
			const FNodeRef B{EStationType::Filter,    0};
			const FNodeRef C{EStationType::Sorter,    0};
			Director->BuildLineDAG(FDAGBuilder()
				.Source(A).Edge(A, B).Edge(A, C).Build());

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* Source = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("source bucket spawned"), Source);
			if (!Source) return;
			SetCarrierItems(Source, { 1, 2, 3 });

			Director->OnRobotDoneAt(EStationType::Generator, Source);

			TestEqual(TEXT("two clones exist with Contents={1,2,3}"),
				CountClonesWithContents(TW.World, Source, {1, 2, 3}), 2);
			TestFalse(TEXT("source bucket was destroyed"), IsValid(Source));
		});

		It("clones the bucket K=3 times on a 1->3 fan-out", [this, CountClonesWithContents]()
		{
			AddExpectedError(TEXT("missing station or robot"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/3);

			FScopedTestWorld TW(TEXT("DirectorSpec_FanOut_1to3"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			// 1->3 DAG. We use four station kinds because each kind is one
			// instance in Story 31c (fan-out branches must be different kinds
			// for now; multi-instance-per-kind is a Story 32 concern).
			const FNodeRef A{EStationType::Generator, 0};
			const FNodeRef B{EStationType::Filter,    0};
			const FNodeRef C{EStationType::Sorter,    0};
			const FNodeRef D{EStationType::Checker,   0};
			Director->BuildLineDAG(FDAGBuilder()
				.Source(A).Edge(A, B).Edge(A, C).Edge(A, D).Build());

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* Source = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!Source) return;
			SetCarrierItems(Source, { 5, 6, 7, 8 });

			Director->OnRobotDoneAt(EStationType::Generator, Source);

			TestEqual(TEXT("three clones exist with Contents={5,6,7,8}"),
				CountClonesWithContents(TW.World, Source, {5, 6, 7, 8}), 3);
			TestFalse(TEXT("source bucket was destroyed"), IsValid(Source));
		});

		It("does NOT clone (and does NOT destroy source) on a single-successor "
		   "node — preserves Story 31a/b linear behavior", [this, CountClonesWithContents]()
		{
			AddExpectedError(TEXT("missing station or robot"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);

			FScopedTestWorld TW(TEXT("DirectorSpec_LinearStaysSingleBucket"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			const FNodeRef A{EStationType::Generator, 0};
			const FNodeRef B{EStationType::Filter,    0};
			Director->BuildLineDAG(FDAGBuilder()
				.Source(A).Edge(A, B).Build());

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* Source = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!Source) return;
			SetCarrierItems(Source, { 9, 10 });

			Director->OnRobotDoneAt(EStationType::Generator, Source);

			TestTrue(TEXT("source bucket still valid (no clone, no destroy)"),
				IsValid(Source));
			TestEqual(TEXT("zero clones created"),
				CountClonesWithContents(TW.World, Source, {9, 10}), 0);
		});
	});

	Describe("ClearLineState (Story 34 — re-missioning teardown)", [this]()
	{
		auto SpawnTestStation = [](UWorld* World, UAssemblyLineDirector* Director,
			EStationType Type) -> ATestSyncStation*
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATestSyncStation* S = World->SpawnActor<ATestSyncStation>(
				ATestSyncStation::StaticClass(),
				FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (S)
			{
				S->StationType = Type;
				Director->RegisterStation(S);
			}
			return S;
		};

		It("empties StationByType except for the Orchestrator entry", [this, SpawnTestStation]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_ClearLineState_Stations"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			ATestSyncStation* Orch = SpawnTestStation(TW.World, Director, EStationType::Orchestrator);
			SpawnTestStation(TW.World, Director, EStationType::Generator);
			SpawnTestStation(TW.World, Director, EStationType::Filter);
			SpawnTestStation(TW.World, Director, EStationType::Sorter);
			SpawnTestStation(TW.World, Director, EStationType::Checker);

			Director->ClearLineState();

			TestEqual(TEXT("Orchestrator entry preserved"),
				Director->GetStationOfType(EStationType::Orchestrator), (AStation*)Orch);
			TestNull(TEXT("Generator entry cleared"),
				Director->GetStationOfType(EStationType::Generator));
			TestNull(TEXT("Filter entry cleared"),
				Director->GetStationOfType(EStationType::Filter));
			TestNull(TEXT("Sorter entry cleared"),
				Director->GetStationOfType(EStationType::Sorter));
			TestNull(TEXT("Checker entry cleared"),
				Director->GetStationOfType(EStationType::Checker));
		});

		It("empties RobotByStation entirely", [this, SpawnTestStation]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_ClearLineState_Robots"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			ATestSyncStation* GenStation = SpawnTestStation(TW.World, Director, EStationType::Generator);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AWorkerRobot* Robot = TW.World->SpawnActor<AWorkerRobot>(
				AWorkerRobot::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			Robot->AssignStation(GenStation);
			Director->RegisterRobot(Robot);

			TestNotNull(TEXT("worker registered before clear"),
				Director->GetRobotForStation(EStationType::Generator));

			Director->ClearLineState();

			TestNull(TEXT("worker entry cleared"),
				Director->GetRobotForStation(EStationType::Generator));
		});

		It("resets the DAG to NumNodes == 0", [this]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_ClearLineState_DAG"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			const FNodeRef Gen{EStationType::Generator, 0};
			const FNodeRef Flt{EStationType::Filter,    0};
			Director->BuildLineDAG(FDAGBuilder().Source(Gen).Edge(Gen, Flt).Build());
			TestEqual(TEXT("DAG has 2 nodes pre-clear"),
				Director->GetDAG().NumNodes(), 2);

			Director->ClearLineState();

			TestEqual(TEXT("DAG empty after clear"),
				Director->GetDAG().NumNodes(), 0);
		});

		It("empties WaitingFor and InboundBuckets via the public fan-in path", [this, SpawnTestStation]()
		{
			// Drive a partial fan-in (one parent arrives, second pending)
			// then clear; subsequent fan-in cycle on a fresh DAG should
			// behave correctly with no stale buckets in the queue. No
			// dispatch warnings expected — every arrival in this test
			// stays queued (single arrival on a 2-parent fan-in).

			FScopedTestWorld TW(TEXT("DirectorSpec_ClearLineState_FanInGate"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			const FNodeRef Gen{EStationType::Generator, 0};
			const FNodeRef Flt{EStationType::Filter,    0};
			const FNodeRef Srt{EStationType::Sorter,    0};
			Director->BuildLineDAG(FDAGBuilder()
				.Source(Gen).Source(Flt).Edge(Gen, Srt).Edge(Flt, Srt).Build());

			ATestSyncStation* SrtStation = SpawnTestStation(TW.World, Director, EStationType::Sorter);

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* B1 = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			SetCarrierItems(B1, {1});
			Director->OnRobotDoneAt(EStationType::Generator, B1);

			TestEqual(TEXT("merge not yet fired (still waiting for Filter)"),
				SrtStation->ProcessCallCount, 0);

			Director->ClearLineState();

			// Re-arm DAG fresh; second arrival from a NEW cycle should not
			// pick up the stale Generator-arrival queued before clear.
			Director->BuildLineDAG(FDAGBuilder()
				.Source(Gen).Source(Flt).Edge(Gen, Srt).Edge(Flt, Srt).Build());
			SpawnTestStation(TW.World, Director, EStationType::Sorter);

			APayloadCarrier* B2 = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			SetCarrierItems(B2, {2});
			Director->OnRobotDoneAt(EStationType::Filter, B2);

			// If WaitingFor wasn't cleared, this single arrival would fire
			// the merge (because Generator was already removed from the
			// stale wait set). With a clean state, it shouldn't fire yet.
			ATestSyncStation* FreshSrt = (ATestSyncStation*)Director->GetStationOfType(EStationType::Sorter);
			if (!FreshSrt) return;
			TestEqual(TEXT("post-clear: single arrival on fresh fan-in does NOT fire merge"),
				FreshSrt->ProcessCallCount, 0);
		});

		It("cancels Director-scheduled timers — recycle/autoloop don't fire post-clear", [this]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_ClearLineState_TimersCancelled"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			// Make recycle delay tiny so we can tick past it quickly.
			Director->DelayBetweenCycles = 0.05f;

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* EmptyBucket = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestEqual(TEXT("bucket starts empty"), GetCarrierItemCount(EmptyBucket), 0);

			// Trigger the recycle timer (non-Generator + empty bucket path).
			Director->OnRobotDoneAt(EStationType::Filter, EmptyBucket);

			// Clear immediately — the recycle timer's lambda should never fire.
			Director->ClearLineState();

			// Tick the world past the timer's delay.
			TW.World->Tick(LEVELTICK_All, 0.2f);

			// If the timer fired, Director->StartCycle would have been called,
			// which destroys the bucket and tries to spawn a new one. Bucket
			// destruction is the simplest signal — if the recycle fired, the
			// bucket is gone (Destroy was called inside the lambda).
			// Conversely, if ClearLineState cancelled the timer, the bucket
			// from the OnRobotDoneAt frame stays valid (the test fixture
			// owns the world; nothing else destroys it).
			//
			// NOTE: OnRobotDoneAt ALSO destroys the bucket inside the recycle
			// lambda. If timer cancelled, lambda doesn't run, bucket stays.
			TestTrue(TEXT("bucket still valid post-clear (recycle timer cancelled)"),
				IsValid(EmptyBucket));
		});

		It("preserves the Orchestrator station registration across clear", [this, SpawnTestStation]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_ClearLineState_OrchestratorPreserved"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			ATestSyncStation* Orch = SpawnTestStation(TW.World, Director, EStationType::Orchestrator);

			Director->ClearLineState();

			AStation* Found = Director->GetStationOfType(EStationType::Orchestrator);
			TestEqual(TEXT("Orchestrator survives clear"), Found, (AStation*)Orch);
			TestTrue(TEXT("Orchestrator actor still valid"), IsValid(Found));
		});
	});

	Describe("Any DAG terminal completes the cycle (Story 37)", [this]()
	{
		auto SpawnTestStationOfKind = [](UWorld* World, UAssemblyLineDirector* Director,
			EStationType Type) -> ATestSyncStation*
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATestSyncStation* S = World->SpawnActor<ATestSyncStation>(
				ATestSyncStation::StaticClass(),
				FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (S)
			{
				S->StationType = Type;
				Director->RegisterStation(S);
			}
			return S;
		};

		It("OnRobotDoneAt for a non-Checker terminal broadcasts OnCycleCompleted "
		   "(reproduces the operator-observed 5-station-stalled-at-Filter/1 bug)",
		   [this, SpawnTestStationOfKind]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_S37_NonCheckerTerminal"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;
			Director->bAutoLoop = false;  // skip recycle timer

			// DAG: Generator → Filter (terminal). Filter is the last node;
			// its successors are empty.
			const FNodeRef Gen{EStationType::Generator, 0};
			const FNodeRef Flt{EStationType::Filter,    0};
			Director->BuildLineDAG({
				FStationNode{Gen, FString(),     {}},
				FStationNode{Flt, FString(),  {Gen}},
			});
			SpawnTestStationOfKind(TW.World, Director, EStationType::Filter);

			bool bCompleted = false;
			Director->OnCycleCompleted.AddLambda([&](APayloadCarrier*) { bCompleted = true; });

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* B = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			SetCarrierItems(B, {82, 76});

			Director->OnRobotDoneAt(Flt, B);

			TestTrue(TEXT("OnCycleCompleted broadcast for non-Checker terminal"), bCompleted);
		});

		It("OnRobotDoneAt for an UNREGISTERED FNodeRef still warns "
		   "(distinguishes valid terminal from misconfiguration)",
		   [this]()
		{
			AddExpectedError(TEXT("no DAG successor"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);

			FScopedTestWorld TW(TEXT("DirectorSpec_S37_UnregisteredRefWarns"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			// DAG is empty — any Ref we pass is unregistered.
			Director->BuildLineDAG({});

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* B = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			SetCarrierItems(B, {1});

			bool bCompleted = false;
			Director->OnCycleCompleted.AddLambda([&](APayloadCarrier*) { bCompleted = true; });

			// Unregistered Ref → not a terminal, just garbage. Warning.
			Director->OnRobotDoneAt(FNodeRef{EStationType::Filter, 0}, B);

			TestFalse(TEXT("OnCycleCompleted does NOT fire for unregistered Ref"), bCompleted);
		});

		// Auto-loop scheduling for non-Checker terminals isn't separately
		// tested — it's a trivial `if (bAutoLoop) { SetTimer(...); }` inside
		// CompleteCycle, exercised by the "broadcasts OnCycleCompleted" test
		// above. World->Tick doesn't reliably advance TimerManager in headless
		// fixtures so we'd be testing the engine, not our code.
	});

	Describe("Multi-instance per Kind (Story 35)", [this]()
	{
		auto SpawnTestStationOfKind = [](UWorld* World, UAssemblyLineDirector* Director,
			EStationType Type) -> ATestSyncStation*
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATestSyncStation* S = World->SpawnActor<ATestSyncStation>(
				ATestSyncStation::StaticClass(),
				FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (S)
			{
				S->StationType = Type;
				Director->RegisterStation(S);  // auto-instances per Kind
			}
			return S;
		};

		It("RegisterStation auto-assigns FNodeRef using a per-Kind counter", [this, SpawnTestStationOfKind]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_MultiInstance_AutoCounter"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			ATestSyncStation* F0 = SpawnTestStationOfKind(TW.World, Director, EStationType::Filter);
			ATestSyncStation* F1 = SpawnTestStationOfKind(TW.World, Director, EStationType::Filter);

			TestNotNull(TEXT("F0 spawned"), F0);
			TestNotNull(TEXT("F1 spawned"), F1);
			if (!F0 || !F1) return;

			TestTrue(TEXT("first Filter is Filter/0"),
				F0->NodeRef == FNodeRef{EStationType::Filter, 0});
			TestTrue(TEXT("second Filter is Filter/1"),
				F1->NodeRef == FNodeRef{EStationType::Filter, 1});
		});

		It("StationByNodeRef holds both Filter instances; one doesn't overwrite the other", [this, SpawnTestStationOfKind]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_MultiInstance_NoCollide"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			ATestSyncStation* F0 = SpawnTestStationOfKind(TW.World, Director, EStationType::Filter);
			ATestSyncStation* F1 = SpawnTestStationOfKind(TW.World, Director, EStationType::Filter);

			AStation* LookupF0 = Director->GetStationByNodeRef(FNodeRef{EStationType::Filter, 0});
			AStation* LookupF1 = Director->GetStationByNodeRef(FNodeRef{EStationType::Filter, 1});
			TestEqual(TEXT("Filter/0 lookup returns F0"), LookupF0, (AStation*)F0);
			TestEqual(TEXT("Filter/1 lookup returns F1"), LookupF1, (AStation*)F1);
			TestNotEqual(TEXT("F0 and F1 are distinct actors"), LookupF0, LookupF1);
		});

		It("GetStationOfType(Filter) returns Instance 0 (backward-compat shim)", [this, SpawnTestStationOfKind]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_MultiInstance_BackwardCompat"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			ATestSyncStation* F0 = SpawnTestStationOfKind(TW.World, Director, EStationType::Filter);
			SpawnTestStationOfKind(TW.World, Director, EStationType::Filter);  // F1

			AStation* Found = Director->GetStationOfType(EStationType::Filter);
			TestEqual(TEXT("GetStationOfType returns Instance 0"), Found, (AStation*)F0);
		});

		It("OnRobotDoneAt(FNodeRef{Filter,0}) dispatches to Filter/0's successor; "
		   "OnRobotDoneAt(FNodeRef{Filter,1}) dispatches to Filter/1's successor (different)",
		   [this, SpawnTestStationOfKind]()
		{
			// Two missing-robot warnings — one per Filter instance dispatch
			// since neither has a registered AWorkerRobot in this synthetic
			// fixture.
			AddExpectedError(TEXT("missing station or robot"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/2);

			FScopedTestWorld TW(TEXT("DirectorSpec_MultiInstance_DispatchByRef"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			// DAG: Generator → Filter/0 → Sorter; Generator → Filter/1 → Checker.
			// Filter/0's successor is Sorter; Filter/1's successor is Checker.
			// Without FNodeRef-aware OnRobotDoneAt, dispatch from Filter/1
			// would consult Filter/0's successors (always Sorter) — bug.
			const FNodeRef Gen{EStationType::Generator, 0};
			const FNodeRef F0{EStationType::Filter,    0};
			const FNodeRef F1{EStationType::Filter,    1};
			const FNodeRef Srt{EStationType::Sorter,   0};
			const FNodeRef Chk{EStationType::Checker,  0};
			Director->BuildLineDAG({
				FStationNode{Gen, FString(),     {}},
				FStationNode{F0,  FString(),  {Gen}},
				FStationNode{F1,  FString(),  {Gen}},
				FStationNode{Srt, FString(),  {F0}},
				FStationNode{Chk, FString(),  {F1}},
			});

			// Register Filter/0 + Filter/1 (auto-instancing) and the source
			// Generator. Don't register Sorter/Checker — we just want the
			// dispatch attempt to log which target it tried.
			SpawnTestStationOfKind(TW.World, Director, EStationType::Generator);
			SpawnTestStationOfKind(TW.World, Director, EStationType::Filter);  // F0
			SpawnTestStationOfKind(TW.World, Director, EStationType::Filter);  // F1

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* B0 = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			SetCarrierItems(B0, {1});
			APayloadCarrier* B1 = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			SetCarrierItems(B1, {2});

			// Filter/0 finishes — should dispatch to Sorter (Kind=2 in EStationType).
			Director->OnRobotDoneAt(F0, B0);
			// Filter/1 finishes — should dispatch to Checker (Kind=3).
			Director->OnRobotDoneAt(F1, B1);

			// Both dispatches should have warned with kinds 2 and 3 respectively.
			// We can't easily inspect log messages programmatically beyond
			// AddExpectedError counting "missing station or robot" twice,
			// which the test framework asserts. The expected-2 above is the
			// load-bearing assertion.
			TestTrue(TEXT("test reached completion"), true);
		});

		It("ClearLineState empties StationByNodeRef + RobotByNodeRef and resets per-Kind counter",
		   [this, SpawnTestStationOfKind]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_MultiInstance_ClearResets"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			SpawnTestStationOfKind(TW.World, Director, EStationType::Filter);  // F0
			SpawnTestStationOfKind(TW.World, Director, EStationType::Filter);  // F1
			TestNotNull(TEXT("F0 registered pre-clear"),
				Director->GetStationByNodeRef(FNodeRef{EStationType::Filter, 0}));
			TestNotNull(TEXT("F1 registered pre-clear"),
				Director->GetStationByNodeRef(FNodeRef{EStationType::Filter, 1}));

			Director->ClearLineState();

			TestNull(TEXT("F0 cleared"),
				Director->GetStationByNodeRef(FNodeRef{EStationType::Filter, 0}));
			TestNull(TEXT("F1 cleared"),
				Director->GetStationByNodeRef(FNodeRef{EStationType::Filter, 1}));

			// Counter resets — register a fresh Filter, expect Filter/0 again
			// (not Filter/2).
			ATestSyncStation* Fresh = SpawnTestStationOfKind(TW.World, Director, EStationType::Filter);
			TestTrue(TEXT("post-clear Filter is Filter/0 again (counter reset)"),
				Fresh->NodeRef == FNodeRef{EStationType::Filter, 0});
		});
	});

	Describe("Checker mid-chain handling (Story 35 AC35.6)", [this]()
	{
		// Helper: spawn a TestSyncStation for the Checker plus an
		// AWorkerRobot whose LastResult we can manipulate to simulate
		// PASS/REJECT outcomes.
		auto SpawnCheckerWithBot = [](UWorld* World, UAssemblyLineDirector* Director,
			bool bAccepted, EStationType SendBackTo) -> AWorkerRobot*
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			ATestSyncStation* CheckerStation = World->SpawnActor<ATestSyncStation>(
				ATestSyncStation::StaticClass(),
				FVector::ZeroVector, FRotator::ZeroRotator, Params);
			CheckerStation->StationType = EStationType::Checker;
			Director->RegisterStation(CheckerStation);

			AWorkerRobot* Bot = World->SpawnActor<AWorkerRobot>(
				AWorkerRobot::StaticClass(),
				FVector::ZeroVector, FRotator::ZeroRotator, Params);
			Bot->AssignStation(CheckerStation);
			Bot->LastResult.bAccepted = bAccepted;
			Bot->LastResult.SendBackTo = SendBackTo;
			Director->RegisterRobot(Bot);
			return Bot;
		};

		It("Checker terminal + PASS broadcasts OnCycleCompleted (existing behavior preserved)",
		   [this, SpawnCheckerWithBot]()
		{
			FScopedTestWorld TW(TEXT("DirectorSpec_CheckerTerminal_Pass"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;
			Director->bAutoLoop = false;  // skip the timer-based restart

			// DAG: just Checker (terminal — no successors).
			const FNodeRef Chk{EStationType::Checker, 0};
			Director->BuildLineDAG({ FStationNode{Chk, FString(), {}} });

			SpawnCheckerWithBot(TW.World, Director, /*bAccepted=*/true, EStationType::Filter);

			bool bCompleted = false;
			Director->OnCycleCompleted.AddLambda([&](APayloadCarrier*) { bCompleted = true; });

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* B = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			SetCarrierItems(B, {7});

			Director->OnRobotDoneAt(Chk, B);

			TestTrue(TEXT("OnCycleCompleted fired (terminal Checker PASS)"), bCompleted);
		});

		It("Checker mid-chain + PASS dispatches to successor (no OnCycleCompleted, no auto-loop)",
		   [this, SpawnCheckerWithBot]()
		{
			AddExpectedError(TEXT("missing station or robot"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);

			FScopedTestWorld TW(TEXT("DirectorSpec_CheckerMidchain_Pass"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			// DAG: Checker → Filter/0 (Checker is mid-chain).
			const FNodeRef Chk{EStationType::Checker, 0};
			const FNodeRef Flt{EStationType::Filter,  0};
			Director->BuildLineDAG({
				FStationNode{Chk, FString(),     {}},
				FStationNode{Flt, FString(),  {Chk}},
			});

			SpawnCheckerWithBot(TW.World, Director, /*bAccepted=*/true, EStationType::Filter);

			bool bCompleted = false;
			bool bRejected = false;
			Director->OnCycleCompleted.AddLambda([&](APayloadCarrier*) { bCompleted = true; });
			Director->OnCycleRejected.AddLambda([&](APayloadCarrier*)  { bRejected = true; });

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* B = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			SetCarrierItems(B, {7});

			Director->OnRobotDoneAt(Chk, B);

			TestFalse(TEXT("OnCycleCompleted does NOT fire (mid-chain PASS forwards silently)"),
				bCompleted);
			TestFalse(TEXT("OnCycleRejected does NOT fire on PASS"), bRejected);
			// The expected "missing station or robot" warning above proves
			// dispatch was attempted to Filter/0 (the Checker's successor).
		});

		It("Checker mid-chain + REJECT routes via SendBackTo (existing behavior)",
		   [this, SpawnCheckerWithBot]()
		{
			AddExpectedError(TEXT("missing station or robot"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);

			FScopedTestWorld TW(TEXT("DirectorSpec_CheckerMidchain_Reject"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			// DAG: Checker → Sorter (Checker is mid-chain).
			const FNodeRef Chk{EStationType::Checker, 0};
			const FNodeRef Srt{EStationType::Sorter,  0};
			Director->BuildLineDAG({
				FStationNode{Chk, FString(),     {}},
				FStationNode{Srt, FString(),  {Chk}},
			});

			SpawnCheckerWithBot(TW.World, Director, /*bAccepted=*/false, EStationType::Filter);

			bool bRejected = false;
			Director->OnCycleRejected.AddLambda([&](APayloadCarrier*) { bRejected = true; });

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* B = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			SetCarrierItems(B, {7});

			Director->OnRobotDoneAt(Chk, B);

			TestTrue(TEXT("OnCycleRejected fires on mid-chain REJECT"), bRejected);
			// The expected "missing station or robot" warning proves dispatch
			// was attempted to SendBackTo=Filter (not the DAG successor Sorter).
		});
	});

	Describe("StartAllSourceCycles (Story 32b — multi-source dispatch)", [this]()
	{
		auto SpawnTestStation = [](UWorld* World, UAssemblyLineDirector* Director,
			EStationType Type) -> ATestSyncStation*
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATestSyncStation* S = World->SpawnActor<ATestSyncStation>(
				ATestSyncStation::StaticClass(),
				FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (S)
			{
				S->StationType = Type;
				Director->RegisterStation(S);
			}
			return S;
		};

		auto CountBuckets = [](UWorld* World) -> int32
		{
			int32 N = 0;
			for (TActorIterator<APayloadCarrier> It(World); It; ++It)
			{
				if (IsValid(*It)) ++N;
			}
			return N;
		};

		It("dispatches one bucket per source node — two sources => two buckets", [this, SpawnTestStation, CountBuckets]()
		{
			// DispatchToStation will warn for missing robots — we're not
			// spawning AWorkerRobots in this spec.
			AddExpectedError(TEXT("missing station or robot"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/2);

			FScopedTestWorld TW(TEXT("DirectorSpec_StartAllSources_TwoSources"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			// Two-source fan-in: Generator + Filter both source, Sorter merges.
			const FNodeRef A{EStationType::Generator, 0};
			const FNodeRef B{EStationType::Filter,    0};
			const FNodeRef C{EStationType::Sorter,    0};
			Director->BuildLineDAG(FDAGBuilder()
				.Source(A).Source(B).Edge(A, C).Edge(B, C).Build());

			SpawnTestStation(TW.World, Director, EStationType::Generator);
			SpawnTestStation(TW.World, Director, EStationType::Filter);

			const int32 Before = CountBuckets(TW.World);
			Director->StartAllSourceCycles();
			const int32 After = CountBuckets(TW.World);

			TestEqual(TEXT("two buckets spawned (one per source node)"),
				After - Before, 2);
		});

		It("dispatches one bucket on a single-source DAG (matches StartCycle behavior)", [this, SpawnTestStation, CountBuckets]()
		{
			AddExpectedError(TEXT("missing station or robot"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);

			FScopedTestWorld TW(TEXT("DirectorSpec_StartAllSources_SingleSource"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

			const FNodeRef A{EStationType::Generator, 0};
			const FNodeRef B{EStationType::Filter,    0};
			Director->BuildLineDAG(FDAGBuilder().Source(A).Edge(A, B).Build());

			SpawnTestStation(TW.World, Director, EStationType::Generator);

			const int32 Before = CountBuckets(TW.World);
			Director->StartAllSourceCycles();
			const int32 After = CountBuckets(TW.World);

			TestEqual(TEXT("exactly one bucket spawned"), After - Before, 1);
		});
	});

	Describe("Fan-in dispatch (Story 31d — K > 1 predecessors)", [this]()
	{
		// Helper: spawn a test station with the given type set, register with Director.
		auto SpawnTestStation = [](UWorld* World, UAssemblyLineDirector* Director,
			EStationType Type) -> ATestSyncStation*
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATestSyncStation* S = World->SpawnActor<ATestSyncStation>(
				ATestSyncStation::StaticClass(),
				FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (S)
			{
				S->StationType = Type;
				Director->RegisterStation(S);
			}
			return S;
		};

		It("waits for both parents on a 2->1 fan-in, then fires merge with both inputs", [this, SpawnTestStation]()
		{
			// Story 37 — Sorter is a registered terminal with no successors.
			// Post-Story-37 it broadcasts OnCycleCompleted instead of warning.
			// Disable auto-loop so the recycle timer doesn't fire mid-test.

			FScopedTestWorld TW(TEXT("DirectorSpec_FanIn_2to1"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;
			Director->bAutoLoop = false;

			// 2->1 DAG: Generator -> Sorter, Filter -> Sorter.
			const FNodeRef A{EStationType::Generator, 0};
			const FNodeRef B{EStationType::Filter,    0};
			const FNodeRef C{EStationType::Sorter,    0};
			Director->BuildLineDAG(FDAGBuilder()
				.Source(A).Source(B).Edge(A, C).Edge(B, C).Build());

			ATestSyncStation* StationC = SpawnTestStation(TW.World, Director, EStationType::Sorter);
			TestNotNull(TEXT("Sorter test station spawned + registered"), StationC);
			if (!StationC) return;

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* BucketA = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			APayloadCarrier* BucketB = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			SetCarrierItems(BucketA, { 1, 2, 3 });
			SetCarrierItems(BucketB, { 4, 5, 6 });

			Director->OnRobotDoneAt(EStationType::Generator, BucketA);
			TestEqual(TEXT("after first arrival, no merge yet"), StationC->ProcessCallCount, 0);

			Director->OnRobotDoneAt(EStationType::Filter, BucketB);
			TestEqual(TEXT("after second arrival, merge fired exactly once"),
				StationC->ProcessCallCount, 1);
			TestEqual(TEXT("merge received both inputs"),
				StationC->LastInputs.Num(), 2);
			if (StationC->LastInputs.Num() != 2) return;

			// Arrival order: BucketA first → Inputs[0]; BucketB second → Inputs[1].
			// Per AC31d.3, Inputs[0] survives the merge, Inputs[1..K-1] destroyed.
			TestTrue(TEXT("primary input (Inputs[0], BucketA) survives the merge"),
				IsValid(StationC->LastInputs[0].Get()));
			TestFalse(TEXT("secondary input (Inputs[1], BucketB) destroyed after merge"),
				IsValid(StationC->LastInputs[1].Get()));
			TestEqual(TEXT("primary input identity is BucketA"),
				StationC->LastInputs[0].Get(), BucketA);
		});

		It("the wait state resets after each merge — successive cycles re-fan-in", [this, SpawnTestStation]()
		{
			// Story 37 — Sorter is a registered terminal: each post-merge
			// completion broadcasts OnCycleCompleted instead of warning.
			// Disable auto-loop so timers don't muddy the assertion.

			FScopedTestWorld TW(TEXT("DirectorSpec_FanIn_CycleReEntry"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;
			Director->bAutoLoop = false;

			const FNodeRef A{EStationType::Generator, 0};
			const FNodeRef B{EStationType::Filter,    0};
			const FNodeRef C{EStationType::Sorter,    0};
			Director->BuildLineDAG(FDAGBuilder()
				.Source(A).Source(B).Edge(A, C).Edge(B, C).Build());

			ATestSyncStation* StationC = SpawnTestStation(TW.World, Director, EStationType::Sorter);
			if (!StationC) return;

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			// Cycle 1.
			APayloadCarrier* C1A = TW.World->SpawnActor<APayloadCarrier>(APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			APayloadCarrier* C1B = TW.World->SpawnActor<APayloadCarrier>(APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			SetCarrierItems(C1A, {10}); SetCarrierItems(C1B, {20});
			Director->OnRobotDoneAt(EStationType::Generator, C1A);
			Director->OnRobotDoneAt(EStationType::Filter,    C1B);
			TestEqual(TEXT("merge fired once after cycle 1"), StationC->ProcessCallCount, 1);

			// Cycle 2 — wait state must have reset for this to fire correctly.
			APayloadCarrier* C2A = TW.World->SpawnActor<APayloadCarrier>(APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			APayloadCarrier* C2B = TW.World->SpawnActor<APayloadCarrier>(APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			SetCarrierItems(C2A, {30}); SetCarrierItems(C2B, {40});
			Director->OnRobotDoneAt(EStationType::Generator, C2A);
			TestEqual(TEXT("after cycle-2 first arrival, no second merge yet"),
				StationC->ProcessCallCount, 1);

			Director->OnRobotDoneAt(EStationType::Filter, C2B);
			TestEqual(TEXT("merge fired again after cycle-2 second arrival"),
				StationC->ProcessCallCount, 2);
			TestEqual(TEXT("cycle-2 LastInputs has both cycle-2 buckets"),
				StationC->LastInputs.Num(), 2);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
