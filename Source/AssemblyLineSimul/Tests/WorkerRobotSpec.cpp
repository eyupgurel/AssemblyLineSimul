#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Bucket.h"
#include "Station.h"
#include "TestStations.h"
#include "WorkerRobot.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

namespace AssemblyLineSimulTests
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FWorldContext* Context = nullptr;

		FScopedTestWorld(const TCHAR* Name)
		{
			World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false, FName(Name));
			Context = &GEngine->CreateNewWorldContext(EWorldType::Game);
			Context->SetCurrentWorld(World);

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

	template <typename TStation>
	static TStation* SpawnStationAt(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<TStation>(TStation::StaticClass(), Location, FRotator::ZeroRotator, Params);
	}

	static AWorkerRobot* SpawnWorkerAt(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AWorkerRobot>(AWorkerRobot::StaticClass(), Location, FRotator::ZeroRotator, Params);
	}

	static ABucket* SpawnBucketAt(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ABucket>(ABucket::StaticClass(), Location, FRotator::ZeroRotator, Params);
	}

	// Drive the worker FSM directly — World->Tick on a transient test world doesn't reliably
	// dispatch actor ticks. Calling AActor::Tick is exactly what the tick group would do.
	template <typename TPredicate>
	static int32 TickWorker(AWorkerRobot* Worker, int32 MaxTicks, float DeltaSeconds, TPredicate bDone)
	{
		for (int32 i = 0; i < MaxTicks; ++i)
		{
			if (bDone()) return i;
			Worker->Tick(DeltaSeconds);
		}
		return MaxTicks;
	}
}

DEFINE_SPEC(FWorkerRobotSpec,
	"AssemblyLineSimul.WorkerRobot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FWorkerRobotSpec::Define()
{
	using namespace AssemblyLineSimulTests;

	Describe("BeginTask", [this]()
	{
		It("completes the FSM exactly once when the station's ProcessBucket fires synchronously", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_Sync"));

			const FVector Origin(0.f, 0.f, 0.f);
			ATestSyncStation* Station = SpawnStationAt<ATestSyncStation>(TW.World, Origin);
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, Origin);
			ABucket* Bucket = SpawnBucketAt(TW.World, Origin);

			Worker->WorkDuration = 0.f;
			Worker->AssignStation(Station);
			Station->InputSlot->SetWorldLocation(Worker->GetActorLocation());
			Station->OutputSlot->SetWorldLocation(Worker->GetActorLocation());

			int32 CompleteCount = 0;
			Worker->BeginTask(Bucket, Station->InputSlot, Station->OutputSlot,
				FWorkerTaskComplete::CreateLambda([&CompleteCount](ABucket*) { ++CompleteCount; }));

			TickWorker(Worker, 100, 0.05f, [&]() { return CompleteCount > 0; });

			TestEqual(TEXT("ProcessBucket call count"), Station->ProcessCallCount, 1);
			TestEqual(TEXT("TaskComplete callback count"), CompleteCount, 1);
		});

		It("invokes ProcessBucket exactly once and waits for the deferred completion", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_Deferred"));

			const FVector Origin(0.f, 0.f, 0.f);
			ATestDeferredStation* Station = SpawnStationAt<ATestDeferredStation>(TW.World, Origin);
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, Origin);
			ABucket* Bucket = SpawnBucketAt(TW.World, Origin);

			Worker->WorkDuration = 0.f;
			Worker->AssignStation(Station);
			Station->InputSlot->SetWorldLocation(Worker->GetActorLocation());
			Station->OutputSlot->SetWorldLocation(Worker->GetActorLocation());

			int32 CompleteCount = 0;
			Worker->BeginTask(Bucket, Station->InputSlot, Station->OutputSlot,
				FWorkerTaskComplete::CreateLambda([&CompleteCount](ABucket*) { ++CompleteCount; }));

			// Tick enough frames for the worker to enter Working and dispatch ProcessBucket once.
			TickWorker(Worker, 20, 0.05f, [&]() { return Station->ProcessCallCount > 0; });

			TestEqual(TEXT("ProcessBucket called once after dispatch"), Station->ProcessCallCount, 1);
			TestEqual(TEXT("TaskComplete has not fired yet"), CompleteCount, 0);

			// Continue ticking; ProcessBucket must NOT be re-invoked while we hold the delegate.
			TickWorker(Worker, 20, 0.05f, [&]() { return false; });
			TestEqual(TEXT("ProcessBucket still called only once during wait"), Station->ProcessCallCount, 1);

			// Fire the captured delegate; worker should complete the FSM.
			Station->FireCapturedDelegate();
			TickWorker(Worker, 100, 0.05f, [&]() { return CompleteCount > 0; });

			TestEqual(TEXT("ProcessBucket call count after completion"), Station->ProcessCallCount, 1);
			TestEqual(TEXT("TaskComplete fired exactly once"), CompleteCount, 1);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
