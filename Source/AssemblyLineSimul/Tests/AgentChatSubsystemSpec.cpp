#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AgentChatSubsystem.h"
#include "AssemblyLineTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

DEFINE_SPEC(FAgentChatSubsystemSpec,
	"AssemblyLineSimul.AgentChatSubsystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FAgentChatSubsystemSpec::Define()
{
	Describe("History", [this]()
	{
		It("appends the user message to only the addressed agent's history", [this]()
		{
			UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
			UAgentChatSubsystem* Sub = NewObject<UAgentChatSubsystem>(GI);
			TestNotNull(TEXT("subsystem instantiated"), Sub);
			if (!Sub) return;

			Sub->SendMessage(EStationType::Filter, TEXT("why did you keep 4?"));

			const TArray<FAgentChatMessage>& Filter = Sub->GetHistory(EStationType::Filter);
			const TArray<FAgentChatMessage>& Sorter = Sub->GetHistory(EStationType::Sorter);

			TestTrue(TEXT("Filter has the user message somewhere in its history"),
				Filter.ContainsByPredicate([](const FAgentChatMessage& M)
				{
					return M.Role == TEXT("user") && M.Text == TEXT("why did you keep 4?");
				}));
			TestEqual(TEXT("Sorter history untouched"), Sorter.Num(), 0);
		});
	});

	Describe("OnRuleUpdated", [this]()
	{
		It("broadcasts with (StationType, NewRule) when a Claude reply contains "
		   "a non-empty new_rule (Story 17 AC17.1)", [this]()
		{
			UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
			UAgentChatSubsystem* Sub = NewObject<UAgentChatSubsystem>(GI);
			TestNotNull(TEXT("subsystem instantiated"), Sub);
			if (!Sub) return;

			int32 BroadcastCount = 0;
			EStationType GotStation = EStationType::Generator;
			FString GotRule;
			Sub->OnRuleUpdated.AddLambda([&](EStationType S, const FString& R)
			{
				++BroadcastCount;
				GotStation = S;
				GotRule = R;
			});

			// Synthesise the JSON body that would have come back from Claude after
			// the user said "from now on only keep the even numbers".
			const FString FakeReply = TEXT(
				"{\"reply\":\"Got it, evens only from now on.\","
				"\"new_rule\":\"Keep only the even numbers; drop everything else.\"}");
			Sub->HandleClaudeResponse(EStationType::Filter, /*bSuccess=*/true, FakeReply);

			TestEqual(TEXT("OnRuleUpdated fired exactly once"), BroadcastCount, 1);
			TestEqual(TEXT("payload station is Filter"), GotStation, EStationType::Filter);
			TestEqual(TEXT("payload rule matches Claude's new_rule"),
				GotRule,
				FString(TEXT("Keep only the even numbers; drop everything else.")));
		});

		It("does NOT broadcast when new_rule is null/missing (plain chat reply)", [this]()
		{
			UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
			UAgentChatSubsystem* Sub = NewObject<UAgentChatSubsystem>(GI);
			if (!Sub) return;

			int32 BroadcastCount = 0;
			Sub->OnRuleUpdated.AddLambda([&](EStationType, const FString&) { ++BroadcastCount; });

			Sub->HandleClaudeResponse(EStationType::Filter, /*bSuccess=*/true,
				TEXT("{\"reply\":\"hello there\",\"new_rule\":null}"));

			TestEqual(TEXT("no broadcast for null new_rule"), BroadcastCount, 0);
		});
	});

	Describe("SpeakResponse", [this]()
	{
		It("records the input into LastSpokenForTesting so external systems "
		   "(e.g. the voice-hail handshake) can be asserted to invoke TTS", [this]()
		{
			UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
			UAgentChatSubsystem* Sub = NewObject<UAgentChatSubsystem>(GI);
			TestNotNull(TEXT("subsystem instantiated"), Sub);
			if (!Sub) return;

			TestEqual(TEXT("starts empty"), Sub->LastSpokenForTesting, FString());
			Sub->SpeakResponse(TEXT("Generator here, reading you loud and clear. Go ahead."));
			TestEqual(TEXT("recorded the spoken text"),
				Sub->LastSpokenForTesting,
				FString(TEXT("Generator here, reading you loud and clear. Go ahead.")));

			Sub->SpeakResponse(TEXT("Filter here, reading you loud and clear. Go ahead."));
			TestEqual(TEXT("overwritten by the latest call"),
				Sub->LastSpokenForTesting,
				FString(TEXT("Filter here, reading you loud and clear. Go ahead.")));
		});
	});

	Describe("BuildPromptForStation", [this]()
	{
		It("includes the station's role, current rule, JSON-reply contract, and the user message", [this]()
		{
			UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
			UAgentChatSubsystem* Sub = NewObject<UAgentChatSubsystem>(GI);
			TestNotNull(TEXT("subsystem instantiated"), Sub);
			if (!Sub) return;

			const FString Prompt = Sub->BuildPromptForStation(
				EStationType::Filter, TEXT("hello agent"));

			TestTrue(TEXT("prompt mentions Filter"),
				Prompt.Contains(TEXT("Filter"), ESearchCase::IgnoreCase));
			TestTrue(TEXT("prompt mentions the rule"),
				Prompt.Contains(TEXT("rule"), ESearchCase::IgnoreCase));
			TestTrue(TEXT("prompt asks for JSON reply with new_rule field"),
				Prompt.Contains(TEXT("new_rule")));
			TestTrue(TEXT("prompt contains the user message"),
				Prompt.Contains(TEXT("hello agent")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
