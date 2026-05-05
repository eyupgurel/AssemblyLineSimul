#include "FullCycleFunctionalTest.h"
#include "AssemblyLineDirector.h"
#include "PayloadCarrier.h"
#include "Station.h"
#include "StationSubclasses.h"
#include "WorkerRobot.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

AFullCycleFunctionalTest::AFullCycleFunctionalTest()
{
	// Budget: every station now does an async LLM call (Generator + Filter + Sorter + Checker),
	// each up to 20s HTTP timeout, on top of WorkDuration per station and potential Checker rework.
	TimeLimit = 240.f;
	Description = TEXT("Generator -> Filter -> Sorter -> Checker cycle completes; no worker stranded mid-task.");
}

void AFullCycleFunctionalTest::StartTest()
{
	Super::StartTest();

	// AC3 verifies FSM completion, not LLM connectivity. Suppress LogClaudeAPI so any
	// warnings from the Anthropic call (auth, credit, network) don't fail the test —
	// SuppressedLogCategories is a static on FAutomationTestBase, set once.
	FAutomationTestBase::SuppressedLogCategories.AddUnique(TEXT("LogClaudeAPI"));

	UWorld* World = GetWorld();
	if (!World)
	{
		FinishTest(EFunctionalTestResult::Failed, TEXT("No world"));
		return;
	}

	UAssemblyLineDirector* Director = World->GetSubsystem<UAssemblyLineDirector>();
	if (!Director)
	{
		FinishTest(EFunctionalTestResult::Failed, TEXT("Director subsystem missing"));
		return;
	}

	const TArray<TSubclassOf<AStation>> Specs = {
		AGeneratorStation::StaticClass(),
		AFilterStation::StaticClass(),
		ASorterStation::StaticClass(),
		ACheckerStation::StaticClass()
	};

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 i = 0; i < Specs.Num(); ++i)
	{
		const FVector Loc = LineOrigin + FVector((float)i * StationSpacing, 0.f, 0.f);
		AStation* Station = World->SpawnActor<AStation>(Specs[i], Loc, FRotator::ZeroRotator, Params);
		if (!Station)
		{
			FinishTest(EFunctionalTestResult::Failed, FString::Printf(TEXT("Station %d spawn failed"), i));
			return;
		}
		Director->RegisterStation(Station);
		SpawnedStations.Add(Station);

		const FVector RobotLoc = Station->WorkerStandPoint
			? Station->WorkerStandPoint->GetComponentLocation()
			: Loc + FVector(-250.f, 0.f, 0.f);
		AWorkerRobot* Robot = World->SpawnActor<AWorkerRobot>(
			AWorkerRobot::StaticClass(), RobotLoc, FRotator::ZeroRotator, Params);
		if (!Robot)
		{
			FinishTest(EFunctionalTestResult::Failed, FString::Printf(TEXT("Robot %d spawn failed"), i));
			return;
		}
		Robot->AssignStation(Station);
		Director->RegisterRobot(Robot);
		SpawnedWorkers.Add(Robot);
	}

	// Disable AutoLoop so the test resolves on the very first accepted bucket.
	Director->bAutoLoop = false;
	CycleCompletedHandle = Director->OnCycleCompleted.AddUObject(
		this, &AFullCycleFunctionalTest::HandleCycleCompleted);

	Director->StartCycle();
}

void AFullCycleFunctionalTest::HandleCycleCompleted(APayloadCarrier*)
{
	if (UWorld* World = GetWorld())
	{
		if (UAssemblyLineDirector* Director = World->GetSubsystem<UAssemblyLineDirector>())
		{
			Director->OnCycleCompleted.Remove(CycleCompletedHandle);
		}
	}
	FinishTest(EFunctionalTestResult::Succeeded, TEXT("Cycle reached Checker accept"));
}
