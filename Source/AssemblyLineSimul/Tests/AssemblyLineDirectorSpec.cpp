#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyLineDirector.h"
#include "Bucket.h"
#include "Station.h"
#include "TestStations.h"
#include "WorkerRobot.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

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
}

#endif // WITH_DEV_AUTOMATION_TESTS
