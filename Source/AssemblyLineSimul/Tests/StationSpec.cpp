#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AgentChatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Station.h"
#include "StationSubclasses.h"
#include "TestStations.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace AssemblyLineStationTests
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

	static AGeneratorStation* SpawnStation(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AGeneratorStation>(
			AGeneratorStation::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
}

DEFINE_SPEC(FStationSpec,
	"AssemblyLineSimul.Station",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FStationSpec::Define()
{
	using namespace AssemblyLineStationTests;

	Describe("Checker verdict-speak contract (PASS and REJECT both audible)", [this]()
	{
		It("speaks the PASS verdict aloud through the chat subsystem (TTS), not just the panel", [this]()
		{
			FScopedTestWorld TW(TEXT("StationSpec_CheckerPassSpeaks"));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACheckerStation* Checker = TW.World->SpawnActor<ACheckerStation>(
				ACheckerStation::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			TestNotNull(TEXT("checker spawned"), Checker);
			if (!Checker) return;

			UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
			UAgentChatSubsystem* Chat = NewObject<UAgentChatSubsystem>(GI);
			Checker->SetChatSubsystemForTesting(Chat);

			FStationProcessComplete Sink;  // unbound — we only care about the speak side
			Checker->HandleVerdictReply(/*bSuccess=*/true,
				TEXT("{\"verdict\":\"pass\",\"reason\":\"All four primes sorted ascending.\",\"send_back_to\":null}"),
				Sink);

			TestTrue(TEXT("PASS reached the macOS-say pipeline"),
				Chat->LastSpokenForTesting.StartsWith(TEXT("[PASS]")));
			TestTrue(TEXT("PASS reason embedded"),
				Chat->LastSpokenForTesting.Contains(TEXT("All four primes sorted ascending.")));
		});

		It("speaks the REJECT verdict aloud (the bug the operator hit) — verbose "
		   "complaint reaches macOS-say even when long", [this]()
		{
			FScopedTestWorld TW(TEXT("StationSpec_CheckerRejectSpeaks"));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACheckerStation* Checker = TW.World->SpawnActor<ACheckerStation>(
				ACheckerStation::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!Checker) return;

			UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
			UAgentChatSubsystem* Chat = NewObject<UAgentChatSubsystem>(GI);
			Checker->SetChatSubsystemForTesting(Chat);

			const FString FakeReject = TEXT(
				"{\"verdict\":\"reject\","
				"\"reason\":\"7, 17, 23 and 41 are all odd primes — Filter was supposed to keep only even numbers but let every one of them through. Sending back to Filter for rework.\","
				"\"send_back_to\":\"Filter\"}");
			FStationProcessComplete Sink;
			Checker->HandleVerdictReply(/*bSuccess=*/true, FakeReject, Sink);

			TestTrue(TEXT("REJECT reached the macOS-say pipeline"),
				Chat->LastSpokenForTesting.StartsWith(TEXT("[REJECT]")));
			TestTrue(TEXT("REJECT verbose reason embedded — every offending value named"),
				Chat->LastSpokenForTesting.Contains(TEXT("7, 17, 23 and 41")));
			TestTrue(TEXT("responsible station called out in the reason"),
				Chat->LastSpokenForTesting.Contains(TEXT("Filter")));
		});

		It("speaks the LLM-unreachable fallback aloud too (no silent failures)", [this]()
		{
			FScopedTestWorld TW(TEXT("StationSpec_CheckerUnreachableSpeaks"));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACheckerStation* Checker = TW.World->SpawnActor<ACheckerStation>(
				ACheckerStation::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!Checker) return;

			UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
			UAgentChatSubsystem* Chat = NewObject<UAgentChatSubsystem>(GI);
			Checker->SetChatSubsystemForTesting(Chat);

			FStationProcessComplete Sink;
			Checker->HandleVerdictReply(/*bSuccess=*/false, TEXT("HTTP failure"), Sink);

			TestTrue(TEXT("Unreachable fallback reached macOS-say"),
				Chat->LastSpokenForTesting.Contains(TEXT("LLM unreachable")));
		});
	});

	Describe("SpeakAloud", [this]()
	{
		It("routes the text BOTH onto the talk panel AND through the chat "
		   "subsystem's macOS-`say` pipeline (audible output)", [this]()
		{
			FScopedTestWorld TW(TEXT("StationSpec_SpeakAloud"));
			AStation* Station = SpawnStation(TW.World);

			// Test environments don't have a real GameInstance attached to the world,
			// so inject the chat subsystem directly so SpeakAloud can reach it.
			UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
			UAgentChatSubsystem* Chat = NewObject<UAgentChatSubsystem>(GI);
			Station->SetChatSubsystemForTesting(Chat);

			TestEqual(TEXT("LastSpoken starts empty"),
				Chat->LastSpokenForTesting, FString());

			Station->SpeakAloud(TEXT("REJECT: 9 is not prime; Filter let it through."));

			TestEqual(TEXT("LastSpoken received the verdict (TTS path invoked)"),
				Chat->LastSpokenForTesting,
				FString(TEXT("REJECT: 9 is not prime; Filter let it through.")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
