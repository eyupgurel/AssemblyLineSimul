#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyLineDirector.h"
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
}

#endif // WITH_DEV_AUTOMATION_TESTS
