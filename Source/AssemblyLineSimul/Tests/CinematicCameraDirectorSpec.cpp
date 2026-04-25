#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyLineDirector.h"
#include "CinematicCameraDirector.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace AssemblyLineCinematicTests
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

	static ACinematicCameraDirector* SpawnDirector(UWorld* World, int32 NumShots, bool bLoop)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACinematicCameraDirector* D = World->SpawnActor<ACinematicCameraDirector>(
			ACinematicCameraDirector::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (!D) return nullptr;
		D->bLoop = bLoop;
		D->Shots.Reset();
		for (int32 i = 0; i < NumShots; ++i)
		{
			FCinematicShot Shot;
			Shot.Location = FVector(static_cast<float>(i) * 100.f, 0.f, 0.f);
			Shot.HoldDuration = 999.f;  // never auto-advance during the test
			Shot.BlendDuration = 0.f;
			D->Shots.Add(Shot);
		}
		return D;
	}
}

DEFINE_SPEC(FCinematicCameraDirectorSpec,
	"AssemblyLineSimul.CinematicCameraDirector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FCinematicCameraDirectorSpec::Define()
{
	using namespace AssemblyLineCinematicTests;

	Describe("AdvanceShot", [this]()
	{
		It("loops through shot indices when bLoop is true", [this]()
		{
			FScopedTestWorld TW(TEXT("CinematicSpec_Loop"));
			ACinematicCameraDirector* D = SpawnDirector(TW.World, 3, /*bLoop=*/true);
			TestNotNull(TEXT("director spawned"), D);
			if (!D) return;

			TArray<int32> Observed;
			Observed.Add(D->GetCurrentShotIndex());
			for (int32 i = 0; i < 5; ++i)
			{
				D->AdvanceShot();
				Observed.Add(D->GetCurrentShotIndex());
			}

			const TArray<int32> Expected = { 0, 1, 2, 0, 1, 2 };
			TestEqual(TEXT("loop sequence length"), Observed.Num(), Expected.Num());
			for (int32 i = 0; i < Expected.Num() && i < Observed.Num(); ++i)
			{
				TestEqual(*FString::Printf(TEXT("loop step %d"), i), Observed[i], Expected[i]);
			}
		});

		It("holds on the last shot when bLoop is false", [this]()
		{
			FScopedTestWorld TW(TEXT("CinematicSpec_Hold"));
			ACinematicCameraDirector* D = SpawnDirector(TW.World, 3, /*bLoop=*/false);
			TestNotNull(TEXT("director spawned"), D);
			if (!D) return;

			TArray<int32> Observed;
			Observed.Add(D->GetCurrentShotIndex());
			for (int32 i = 0; i < 5; ++i)
			{
				D->AdvanceShot();
				Observed.Add(D->GetCurrentShotIndex());
			}

			const TArray<int32> Expected = { 0, 1, 2, 2, 2, 2 };
			TestEqual(TEXT("hold sequence length"), Observed.Num(), Expected.Num());
			for (int32 i = 0; i < Expected.Num() && i < Observed.Num(); ++i)
			{
				TestEqual(*FString::Printf(TEXT("hold step %d"), i), Observed[i], Expected[i]);
			}
		});
	});

	Describe("Reactive jumps", [this]()
	{
		It("does NOT jump on OnCheckerStarted (Checker treated like other stations)", [this]()
		{
			FScopedTestWorld TW(TEXT("CinematicSpec_NoCheckerJump"));
			UAssemblyLineDirector* AsmDirector = TW.World->GetSubsystem<UAssemblyLineDirector>();
			TestNotNull(TEXT("AssemblyLineDirector subsystem available"), AsmDirector);
			if (!AsmDirector) return;

			ACinematicCameraDirector* CinDirector = SpawnDirector(TW.World, 3, /*bLoop=*/true);
			CinDirector->CheckerShotIndex = 2;
			CinDirector->ResumeShotIndex = 0;
			CinDirector->BindToAssemblyLine(AsmDirector);

			const int32 InitialIdx = CinDirector->GetCurrentShotIndex();
			AsmDirector->OnCheckerStarted.Broadcast();

			TestEqual(TEXT("shot index unchanged after OnCheckerStarted"),
				CinDirector->GetCurrentShotIndex(), InitialIdx);
		});

		It("jumps to StationCloseupShotIndex[N] when Director broadcasts OnStationActive(N)", [this]()
		{
			FScopedTestWorld TW(TEXT("CinematicSpec_StationActive"));
			UAssemblyLineDirector* AsmDirector = TW.World->GetSubsystem<UAssemblyLineDirector>();
			TestNotNull(TEXT("AssemblyLineDirector subsystem"), AsmDirector);
			if (!AsmDirector) return;

			ACinematicCameraDirector* CinDirector = SpawnDirector(TW.World, 5, /*bLoop=*/true);
			CinDirector->StationCloseupShotIndex.Add(EStationType::Sorter, 3);
			CinDirector->ResumeShotIndex = 0;
			CinDirector->BindToAssemblyLine(AsmDirector);

			AsmDirector->OnStationActive.Broadcast(EStationType::Sorter);

			TestEqual(TEXT("jumped to mapped shot"),
				CinDirector->GetCurrentShotIndex(), 3);
		});

		It("returns to ResumeShotIndex when Director broadcasts OnStationIdle(N)", [this]()
		{
			FScopedTestWorld TW(TEXT("CinematicSpec_StationIdle"));
			UAssemblyLineDirector* AsmDirector = TW.World->GetSubsystem<UAssemblyLineDirector>();
			TestNotNull(TEXT("AssemblyLineDirector subsystem"), AsmDirector);
			if (!AsmDirector) return;

			ACinematicCameraDirector* CinDirector = SpawnDirector(TW.World, 5, /*bLoop=*/true);
			CinDirector->StationCloseupShotIndex.Add(EStationType::Sorter, 3);
			CinDirector->ResumeShotIndex = 1;  // distinct from initial 0 to discriminate
			CinDirector->BindToAssemblyLine(AsmDirector);

			AsmDirector->OnStationActive.Broadcast(EStationType::Sorter);
			AsmDirector->OnStationIdle.Broadcast(EStationType::Sorter);

			TestEqual(TEXT("returned to ResumeShotIndex"),
				CinDirector->GetCurrentShotIndex(), CinDirector->ResumeShotIndex);
		});

		It("returns to ResumeShotIndex when AssemblyLineDirector broadcasts OnCycleCompleted", [this]()
		{
			FScopedTestWorld TW(TEXT("CinematicSpec_Resume"));
			UAssemblyLineDirector* AsmDirector = TW.World->GetSubsystem<UAssemblyLineDirector>();
			TestNotNull(TEXT("AssemblyLineDirector subsystem available"), AsmDirector);
			if (!AsmDirector) return;

			ACinematicCameraDirector* CinDirector = SpawnDirector(TW.World, 3, /*bLoop=*/true);
			CinDirector->CheckerShotIndex = 2;
			CinDirector->ResumeShotIndex = 1;  // distinct from initial 0 so a stuck stub doesn't pass
			CinDirector->BindToAssemblyLine(AsmDirector);

			AsmDirector->OnCheckerStarted.Broadcast();
			AsmDirector->OnCycleCompleted.Broadcast(nullptr);

			TestEqual(TEXT("returned to ResumeShotIndex"),
				CinDirector->GetCurrentShotIndex(), CinDirector->ResumeShotIndex);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
