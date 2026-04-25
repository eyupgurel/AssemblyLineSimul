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

	Describe("BuildPromptForStation", [this]()
	{
		It("includes the station's role and the user message in the prompt", [this]()
		{
			UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
			UAgentChatSubsystem* Sub = NewObject<UAgentChatSubsystem>(GI);
			TestNotNull(TEXT("subsystem instantiated"), Sub);
			if (!Sub) return;

			const FString Prompt = Sub->BuildPromptForStation(
				EStationType::Filter, TEXT("hello agent"));

			TestTrue(TEXT("prompt mentions Filter"),
				Prompt.Contains(TEXT("Filter"), ESearchCase::IgnoreCase));
			TestTrue(TEXT("prompt mentions prime"),
				Prompt.Contains(TEXT("prime"), ESearchCase::IgnoreCase));
			TestTrue(TEXT("prompt contains the user message"),
				Prompt.Contains(TEXT("hello agent")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
