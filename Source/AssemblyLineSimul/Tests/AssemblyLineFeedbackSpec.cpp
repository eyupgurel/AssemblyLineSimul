#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyLineDirector.h"
#include "AssemblyLineFeedback.h"
#include "AssemblyLineGameMode.h"
#include "Bucket.h"
#include "Components/PointLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace AssemblyLineFeedbackTests
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

	static APointLight* FindFirstPointLight(UWorld* World)
	{
		for (TActorIterator<APointLight> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}
}

DEFINE_SPEC(FAssemblyLineFeedbackSpec,
	"AssemblyLineSimul.AssemblyLineFeedback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FAssemblyLineFeedbackSpec::Define()
{
	using namespace AssemblyLineFeedbackTests;

	Describe("HandleCycleRejected", [this]()
	{
		It("spawns a red point light near the bucket location", [this]()
		{
			FScopedTestWorld TW(TEXT("FeedbackSpec_Reject"));
			UAssemblyLineDirector* AsmDirector = TW.World->GetSubsystem<UAssemblyLineDirector>();
			TestNotNull(TEXT("AsmDirector subsystem"), AsmDirector);
			if (!AsmDirector) return;

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineFeedback* Feedback = TW.World->SpawnActor<AAssemblyLineFeedback>(
				AAssemblyLineFeedback::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			Feedback->BindToAssemblyLine(AsmDirector);

			ABucket* Bucket = TW.World->SpawnActor<ABucket>(
				ABucket::StaticClass(), FVector(500.f, 0.f, 100.f), FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("test bucket"), Bucket);
			if (!Bucket) return;

			AsmDirector->OnCycleRejected.Broadcast(Bucket);

			APointLight* Light = FindFirstPointLight(TW.World);
			TestNotNull(TEXT("a point light was spawned"), Light);
			if (Light)
			{
				const FVector Delta = Light->GetActorLocation() - Bucket->GetActorLocation();
				TestTrue(TEXT("light is near the bucket"), Delta.Size() < 200.f);
				if (UPointLightComponent* C = Light->PointLightComponent)
				{
					TestTrue(TEXT("light is reddish"),
						C->GetLightColor().R > C->GetLightColor().G && C->GetLightColor().R > C->GetLightColor().B);
				}
			}
		});
	});

	Describe("HandleCycleCompleted", [this]()
	{
		It("spawns a green point light near the bucket location", [this]()
		{
			FScopedTestWorld TW(TEXT("FeedbackSpec_Accept"));
			UAssemblyLineDirector* AsmDirector = TW.World->GetSubsystem<UAssemblyLineDirector>();
			TestNotNull(TEXT("AsmDirector subsystem"), AsmDirector);
			if (!AsmDirector) return;

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineFeedback* Feedback = TW.World->SpawnActor<AAssemblyLineFeedback>(
				AAssemblyLineFeedback::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			Feedback->BindToAssemblyLine(AsmDirector);

			ABucket* Bucket = TW.World->SpawnActor<ABucket>(
				ABucket::StaticClass(), FVector(800.f, 200.f, 100.f), FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("test bucket"), Bucket);
			if (!Bucket) return;

			AsmDirector->OnCycleCompleted.Broadcast(Bucket);

			APointLight* Light = FindFirstPointLight(TW.World);
			TestNotNull(TEXT("a point light was spawned"), Light);
			if (Light)
			{
				const FVector Delta = Light->GetActorLocation() - Bucket->GetActorLocation();
				TestTrue(TEXT("light is near the bucket"), Delta.Size() < 200.f);
				if (UPointLightComponent* C = Light->PointLightComponent)
				{
					TestTrue(TEXT("light is greenish"),
						C->GetLightColor().G > C->GetLightColor().R && C->GetLightColor().G > C->GetLightColor().B);
				}
			}
		});
	});

	Describe("GameMode.SpawnFeedback", [this]()
	{
		It("spawns exactly one AAssemblyLineFeedback actor", [this]()
		{
			FScopedTestWorld TW(TEXT("FeedbackSpec_GameModeSpawn"));

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AAssemblyLineGameMode* GM = TW.World->SpawnActor<AAssemblyLineGameMode>(
				AAssemblyLineGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("GameMode spawned"), GM);
			if (!GM) return;

			GM->SpawnFeedback();

			int32 Count = 0;
			for (TActorIterator<AAssemblyLineFeedback> It(TW.World); It; ++It)
			{
				++Count;
			}
			TestEqual(TEXT("exactly one feedback actor"), Count, 1);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
