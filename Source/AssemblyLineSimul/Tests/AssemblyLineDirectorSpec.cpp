#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyLineDirector.h"
#include "Bucket.h"
#include "DAG/AssemblyLineDAG.h"
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
			Director->BuildLineDAG({
				FStationNode{A, FString(),    {}},
				FStationNode{B, FString(),  {A}},
				FStationNode{C, FString(),  {A}},
			});

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
			Director->BuildLineDAG({
				FStationNode{A, FString(),    {}},
				FStationNode{B, FString(),  {A}},
				FStationNode{C, FString(),  {A}},
				FStationNode{D, FString(),  {A}},
			});

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
			Director->BuildLineDAG({
				FStationNode{A, FString(),    {}},
				FStationNode{B, FString(),  {A}},
			});

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
}

#endif // WITH_DEV_AUTOMATION_TESTS
