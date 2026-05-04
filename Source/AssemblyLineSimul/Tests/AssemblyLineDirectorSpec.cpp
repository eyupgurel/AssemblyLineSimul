#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyLineDirector.h"
#include "Bucket.h"
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
}

DEFINE_SPEC(FAssemblyLineDirectorSpec,
	"AssemblyLineSimul.AssemblyLineDirector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FAssemblyLineDirectorSpec::Define()
{
	using namespace AssemblyLineDirectorTests;

	Describe("RegisterRobot", [this]()
	{
		It("re-broadcasts OnStationActive when a registered worker fires OnStartedWorking", [this]()
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
			Director->RegisterRobot(Worker);

			EStationType Captured = EStationType::Generator;
			bool bFired = false;
			Director->OnStationActive.AddLambda([&Captured, &bFired](EStationType St)
			{
				Captured = St;
				bFired = true;
			});

			Worker->OnStartedWorking.Broadcast(EStationType::Sorter);

			TestTrue(TEXT("Director re-broadcast OnStationActive"), bFired);
			TestEqual(TEXT("station type propagated"),
				static_cast<int32>(Captured), static_cast<int32>(EStationType::Sorter));
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
			ABucket* Bucket = TW.World->SpawnActor<ABucket>(
				ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("bucket spawned"), Bucket);
			TestEqual(TEXT("bucket starts empty"), Bucket->Contents.Num(), 0);

			bool bRecycled = false;
			ABucket* RecycledArg = nullptr;
			Director->OnCycleRecycled.AddLambda([&](ABucket* B)
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
			ABucket* Bucket = TW.World->SpawnActor<ABucket>(
				ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			Bucket->Contents = { 7, 11, 13 };

			bool bRecycled = false;
			Director->OnCycleRecycled.AddLambda([&](ABucket*) { bRecycled = true; });

			Director->OnRobotDoneAt(EStationType::Filter, Bucket);

			TestFalse(TEXT("OnCycleRecycled does NOT fire for non-empty bucket"), bRecycled);
		});
	});

	Describe("Fan-out dispatch (Story 31c — K > 1 successors)", [this]()
	{
		// Helper: count buckets currently in the world that look like dispatched
		// fan-out clones — non-source, non-pending-kill, with Contents matching
		// the supplied set.
		auto CountClonesWithContents = [](UWorld* World, const ABucket* Source,
			const TArray<int32>& ExpectedContents) -> int32
		{
			int32 N = 0;
			for (TActorIterator<ABucket> It(World); It; ++It)
			{
				ABucket* B = *It;
				if (!IsValid(B) || B == Source) continue;
				if (B->Contents == ExpectedContents) ++N;
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
			ABucket* Source = TW.World->SpawnActor<ABucket>(
				ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("source bucket spawned"), Source);
			if (!Source) return;
			Source->Contents = { 1, 2, 3 };

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
			ABucket* Source = TW.World->SpawnActor<ABucket>(
				ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!Source) return;
			Source->Contents = { 5, 6, 7, 8 };

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
			ABucket* Source = TW.World->SpawnActor<ABucket>(
				ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!Source) return;
			Source->Contents = { 9, 10 };

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
			ABucket* B1 = TW.World->SpawnActor<ABucket>(
				ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			B1->Contents = {1};
			Director->OnRobotDoneAt(EStationType::Generator, B1);

			TestEqual(TEXT("merge not yet fired (still waiting for Filter)"),
				SrtStation->ProcessCallCount, 0);

			Director->ClearLineState();

			// Re-arm DAG fresh; second arrival from a NEW cycle should not
			// pick up the stale Generator-arrival queued before clear.
			Director->BuildLineDAG(FDAGBuilder()
				.Source(Gen).Source(Flt).Edge(Gen, Srt).Edge(Flt, Srt).Build());
			SpawnTestStation(TW.World, Director, EStationType::Sorter);

			ABucket* B2 = TW.World->SpawnActor<ABucket>(
				ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			B2->Contents = {2};
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
			ABucket* EmptyBucket = TW.World->SpawnActor<ABucket>(
				ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestEqual(TEXT("bucket starts empty"), EmptyBucket->Contents.Num(), 0);

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
			for (TActorIterator<ABucket> It(World); It; ++It)
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
			// After merge, OnRobotDoneAt continues from the merge target (Sorter)
			// which has no successor in this DAG — expect one warning.
			AddExpectedError(TEXT("no DAG successor"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);

			FScopedTestWorld TW(TEXT("DirectorSpec_FanIn_2to1"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

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
			ABucket* BucketA = TW.World->SpawnActor<ABucket>(
				ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			ABucket* BucketB = TW.World->SpawnActor<ABucket>(
				ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			BucketA->Contents = { 1, 2, 3 };
			BucketB->Contents = { 4, 5, 6 };

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
			// Each cycle's post-merge OnRobotDoneAt(Sorter, ...) hits the
			// no-successor branch — two cycles → two warnings.
			AddExpectedError(TEXT("no DAG successor"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/2);

			FScopedTestWorld TW(TEXT("DirectorSpec_FanIn_CycleReEntry"));
			UAssemblyLineDirector* Director = TW.World->GetSubsystem<UAssemblyLineDirector>();
			if (!Director) return;

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
			ABucket* C1A = TW.World->SpawnActor<ABucket>(ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			ABucket* C1B = TW.World->SpawnActor<ABucket>(ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			C1A->Contents = {10}; C1B->Contents = {20};
			Director->OnRobotDoneAt(EStationType::Generator, C1A);
			Director->OnRobotDoneAt(EStationType::Filter,    C1B);
			TestEqual(TEXT("merge fired once after cycle 1"), StationC->ProcessCallCount, 1);

			// Cycle 2 — wait state must have reset for this to fire correctly.
			ABucket* C2A = TW.World->SpawnActor<ABucket>(ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			ABucket* C2B = TW.World->SpawnActor<ABucket>(ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			C2A->Contents = {30}; C2B->Contents = {40};
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
