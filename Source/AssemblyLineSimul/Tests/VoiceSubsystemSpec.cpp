#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AgentChatSubsystem.h"
#include "AssemblyLineTypes.h"
#include "Engine/GameInstance.h"
#include "VoiceSubsystem.h"

DEFINE_SPEC(FVoiceSubsystemSpec,
	"AssemblyLineSimul.VoiceSubsystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace
{
	// Helper: builds a transient UGameInstance with both subsystems attached.
	struct FVoiceTestEnv
	{
		UGameInstance* GI = nullptr;
		UVoiceSubsystem* Voice = nullptr;
		UAgentChatSubsystem* Chat = nullptr;

		FVoiceTestEnv()
		{
			GI = NewObject<UGameInstance>(GetTransientPackage());
			Voice = NewObject<UVoiceSubsystem>(GI);
			Chat  = NewObject<UAgentChatSubsystem>(GI);
			Voice->SetChatSubsystemForTesting(Chat);
		}
	};
}

void FVoiceSubsystemSpec::Define()
{
	Describe("Initial state", [this]()
	{
		It("starts with no active agent", [this]()
		{
			FVoiceTestEnv Env;
			TestFalse(TEXT("no active agent at construction"), Env.Voice->HasActiveAgent());
		});
	});

	Describe("Hailing", [this]()
	{
		It("sets the active agent and broadcasts on a successful hail", [this]()
		{
			FVoiceTestEnv Env;
			bool bBroadcast = false;
			EStationType Broadcasted = EStationType::Generator;
			Env.Voice->OnActiveAgentChanged.AddLambda([&](EStationType A)
			{
				bBroadcast = true;
				Broadcasted = A;
			});

			Env.Voice->HandleTranscript(TEXT("hey filter do you read me"));

			TestTrue(TEXT("active agent now set"), Env.Voice->HasActiveAgent());
			TestEqual(TEXT("active is Filter"), Env.Voice->GetActiveAgent(), EStationType::Filter);
			TestTrue(TEXT("broadcast fired"), bBroadcast);
			TestEqual(TEXT("broadcast payload is Filter"), Broadcasted, EStationType::Filter);
		});

		It("switches active agent when a different hail arrives", [this]()
		{
			FVoiceTestEnv Env;
			Env.Voice->HandleTranscript(TEXT("hey filter do you read me"));
			Env.Voice->HandleTranscript(TEXT("hey sorter do you read me"));

			TestEqual(TEXT("active is now Sorter"), Env.Voice->GetActiveAgent(), EStationType::Sorter);
		});
	});

	Describe("Command routing", [this]()
	{
		It("routes a non-hail transcript to the active agent's chat history", [this]()
		{
			FVoiceTestEnv Env;
			Env.Voice->HandleTranscript(TEXT("hey filter do you read me"));
			Env.Voice->HandleTranscript(TEXT("only filter the odd numbers"));

			const TArray<FAgentChatMessage>& History = Env.Chat->GetHistory(EStationType::Filter);
			TestTrue(TEXT("Filter received the user command"),
				History.ContainsByPredicate([](const FAgentChatMessage& M)
				{
					return M.Role == TEXT("user") && M.Text.Contains(TEXT("odd numbers"));
				}));
		});

		It("does not route commands when no agent is active", [this]()
		{
			FVoiceTestEnv Env;
			Env.Voice->HandleTranscript(TEXT("only filter the odd numbers"));

			TestEqual(TEXT("no Filter history"),
				Env.Chat->GetHistory(EStationType::Filter).Num(), 0);
			TestEqual(TEXT("no Sorter history"),
				Env.Chat->GetHistory(EStationType::Sorter).Num(), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
