#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AgentPromptLibrary.h"
#include "AssemblyLineTypes.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_SPEC(FAgentPromptLibrarySpec,
	"AssemblyLineSimul.AgentPromptLibrary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FAgentPromptLibrarySpec::Define()
{
	Describe("FormatPrompt", [this]()
	{
		It("substitutes {{name}} placeholders with the provided value", [this]()
		{
			const FString Out = AgentPromptLibrary::FormatPrompt(
				TEXT("hello {{name}}, the count is {{count}}"),
				{ {TEXT("name"), TEXT("world")}, {TEXT("count"), TEXT("42")} });
			TestEqual(TEXT("substituted"), Out, FString(TEXT("hello world, the count is 42")));
		});

		It("leaves an unresolved placeholder intact in the output (and logs a warning)", [this]()
		{
			const FString Out = AgentPromptLibrary::FormatPrompt(
				TEXT("hello {{missing}}"),
				{ {TEXT("name"), TEXT("world")} });
			TestEqual(TEXT("unresolved kept verbatim"), Out, FString(TEXT("hello {{missing}}")));
		});

		It("returns the template unchanged when Vars is empty and template has no placeholders", [this]()
		{
			const FString Out = AgentPromptLibrary::FormatPrompt(TEXT("plain text"), {});
			TestEqual(TEXT("identity"), Out, FString(TEXT("plain text")));
		});
	});

	Describe("LoadAgentSection", [this]()
	{
		// Helper: wipe Saved/Agents/<Kind>.md AND invalidate cache so the
		// "verbatim from Content/Agents" tests aren't poisoned by an
		// orchestrator-authored Saved/ override left behind by a recent
		// PIE/packaged-app session (Story 33b loader prefers Saved/).
		auto FreshContentLoad = []()
		{
			const FString SavedAgents = FPaths::ProjectSavedDir() / TEXT("Agents");
			for (const TCHAR* F : {TEXT("Generator.md"), TEXT("Filter.md"),
				TEXT("Sorter.md"), TEXT("Checker.md")})
			{
				IFileManager::Get().Delete(*(SavedAgents / F));
			}
			AgentPromptLibrary::InvalidateCache();
		};

		It("returns the Generator's DefaultRule from Generator.md verbatim", [this, FreshContentLoad]()
		{
			FreshContentLoad();
			const FString Rule = AgentPromptLibrary::LoadAgentSection(
				EStationType::Generator, TEXT("DefaultRule"));
			TestEqual(TEXT("Generator default rule"), Rule,
				FString(TEXT("Generate 10 random integers in the range 1 to 100")));
		});

		It("returns the Filter's Role description from Filter.md verbatim", [this, FreshContentLoad]()
		{
			FreshContentLoad();
			const FString Role = AgentPromptLibrary::LoadAgentSection(
				EStationType::Filter, TEXT("Role"));
			TestEqual(TEXT("Filter role"), Role,
				FString(TEXT("You inspect each number in the input bucket and keep or remove items according to your rule.")));
		});

		It("returns the Generator's ProcessBucketPrompt with the {{rule}} placeholder still in place", [this]()
		{
			const FString Prompt = AgentPromptLibrary::LoadAgentSection(
				EStationType::Generator, TEXT("ProcessBucketPrompt"));
			TestTrue(TEXT("prompt opens with the standard preamble"),
				Prompt.StartsWith(TEXT("You are the Generator agent on an assembly line.")));
			TestTrue(TEXT("prompt contains the {{rule}} placeholder"),
				Prompt.Contains(TEXT("{{rule}}")));
			TestTrue(TEXT("prompt asks for {\"result\":[...]} JSON"),
				Prompt.Contains(TEXT("{\"result\":[<integers>]}")));
		});

		It("returns empty for a section that does not exist (and logs a warning)", [this]()
		{
			const FString Out = AgentPromptLibrary::LoadAgentSection(
				EStationType::Generator, TEXT("ThisSectionDoesNotExist"));
			TestEqual(TEXT("missing section returns empty"), Out, FString());
		});
	});

	Describe("LoadChatSection", [this]()
	{
		It("returns the ChatPromptTemplate with the {{agent}}, {{role}}, {{rule}} placeholders", [this]()
		{
			const FString Tpl = AgentPromptLibrary::LoadChatSection(TEXT("ChatPromptTemplate"));
			TestTrue(TEXT("contains {{agent}}"), Tpl.Contains(TEXT("{{agent}}")));
			TestTrue(TEXT("contains {{role}}"),  Tpl.Contains(TEXT("{{role}}")));
			TestTrue(TEXT("contains {{rule}}"),  Tpl.Contains(TEXT("{{rule}}")));
			TestTrue(TEXT("contains {{message}}"), Tpl.Contains(TEXT("{{message}}")));
		});
	});

	Describe("Orchestrator Mission section (Story 33a)", [this]()
	{
		It("exposes a Mission section with non-empty plain-English content", [this]()
		{
			const FString Mission = AgentPromptLibrary::LoadAgentSection(
				EStationType::Orchestrator, TEXT("Mission"));
			TestFalse(TEXT("Mission section is non-empty"), Mission.IsEmpty());
			TestFalse(TEXT("Mission is plain English (no JSON braces)"),
				Mission.Contains(TEXT("{")));
			TestFalse(TEXT("Mission is operator-voice (no Claude-instruction leakage)"),
				Mission.Contains(TEXT("You are")) || Mission.Contains(TEXT("you are")));
		});

		It("default Mission describes the canonical 4-station pipeline", [this]()
		{
			const FString Mission = AgentPromptLibrary::LoadAgentSection(
				EStationType::Orchestrator, TEXT("Mission"));
			TestTrue(TEXT("mentions generating numbers"),
				Mission.Contains(TEXT("Generate"), ESearchCase::IgnoreCase) ||
				Mission.Contains(TEXT("integers"), ESearchCase::IgnoreCase));
			TestTrue(TEXT("mentions filtering primes"),
				Mission.Contains(TEXT("filter"), ESearchCase::IgnoreCase) &&
				Mission.Contains(TEXT("prime"), ESearchCase::IgnoreCase));
			TestTrue(TEXT("mentions sorting"),
				Mission.Contains(TEXT("sort"), ESearchCase::IgnoreCase));
			TestTrue(TEXT("mentions checking"),
				Mission.Contains(TEXT("check"), ESearchCase::IgnoreCase));
		});
	});

	Describe("Saved/Agents precedence over Content/Agents (Story 33b)", [this]()
	{
		// Each test in this Describe writes to Saved/Agents/<Kind>.md. The
		// AgentPromptLibrary cache is process-lifetime, so we must invalidate
		// before AND after each test to avoid leaking state into adjacent
		// tests (and to force a fresh disk read).
		auto SavedAgentsDir = []()
		{
			return FPaths::ProjectSavedDir() / TEXT("Agents");
		};

		auto WriteSavedAgent = [SavedAgentsDir](EStationType Kind, const FString& Body)
		{
			const TCHAR* Filename = TEXT("");
			switch (Kind)
			{
			case EStationType::Generator:    Filename = TEXT("Generator.md");    break;
			case EStationType::Filter:       Filename = TEXT("Filter.md");       break;
			case EStationType::Sorter:       Filename = TEXT("Sorter.md");       break;
			case EStationType::Checker:      Filename = TEXT("Checker.md");      break;
			case EStationType::Orchestrator: Filename = TEXT("Orchestrator.md"); break;
			}
			IFileManager::Get().MakeDirectory(*SavedAgentsDir(), /*Tree=*/true);
			const FString Path = SavedAgentsDir() / Filename;
			FFileHelper::SaveStringToFile(Body, *Path);
			return Path;
		};

		auto DeleteSavedAgent = [SavedAgentsDir](EStationType Kind)
		{
			const TCHAR* Filename = TEXT("");
			switch (Kind)
			{
			case EStationType::Generator:    Filename = TEXT("Generator.md");    break;
			case EStationType::Filter:       Filename = TEXT("Filter.md");       break;
			case EStationType::Sorter:       Filename = TEXT("Sorter.md");       break;
			case EStationType::Checker:      Filename = TEXT("Checker.md");      break;
			case EStationType::Orchestrator: Filename = TEXT("Orchestrator.md"); break;
			}
			const FString Path = SavedAgentsDir() / Filename;
			IFileManager::Get().Delete(*Path);
		};

		It("Saved/Agents/<Kind>.md takes precedence over Content/Agents/<Kind>.md", [this, WriteSavedAgent, DeleteSavedAgent]()
		{
			// Sorter.md in Content has DefaultRule "Sort the bucket strictly ascending."
			// We write a different DefaultRule in Saved/ and assert the loader returns it.
			const FString SavedBody = TEXT(
				"# Sorter agent (saved override for Story 33b test)\n\n"
				"## DefaultRule\n"
				"OVERRIDDEN_BY_SAVED\n\n"
				"## Role\n"
				"Saved-tier role text.\n\n"
				"## ProcessBucketPrompt\n"
				"(unused in this test)\n");
			WriteSavedAgent(EStationType::Sorter, SavedBody);
			AgentPromptLibrary::InvalidateCache();

			const FString Rule = AgentPromptLibrary::LoadAgentSection(
				EStationType::Sorter, TEXT("DefaultRule"));
			TestEqual(TEXT("Saved/ DefaultRule wins"), Rule, FString(TEXT("OVERRIDDEN_BY_SAVED")));

			// Cleanup so adjacent tests see the Content fallback.
			DeleteSavedAgent(EStationType::Sorter);
			AgentPromptLibrary::InvalidateCache();
		});

		It("falls back to Content/Agents/ when Saved/Agents/<Kind>.md is absent", [this, DeleteSavedAgent]()
		{
			DeleteSavedAgent(EStationType::Sorter);
			AgentPromptLibrary::InvalidateCache();
			const FString Rule = AgentPromptLibrary::LoadAgentSection(
				EStationType::Sorter, TEXT("DefaultRule"));
			// Content/Agents/Sorter.md DefaultRule is the canonical demo text.
			TestTrue(TEXT("Content fallback returns the canonical Sorter rule"),
				Rule.Contains(TEXT("ascending"), ESearchCase::IgnoreCase) ||
				Rule.Contains(TEXT("sort"),      ESearchCase::IgnoreCase));
		});

		It("InvalidateCache forces a fresh disk read on the next LoadAgentSection", [this, WriteSavedAgent, DeleteSavedAgent]()
		{
			// First, prime the cache from Content/.
			DeleteSavedAgent(EStationType::Sorter);
			AgentPromptLibrary::InvalidateCache();
			const FString FromContent = AgentPromptLibrary::LoadAgentSection(
				EStationType::Sorter, TEXT("DefaultRule"));

			// Now write a Saved override but DON'T invalidate yet — cache
			// should still serve the Content value.
			WriteSavedAgent(EStationType::Sorter,
				TEXT("# Sorter\n\n## DefaultRule\nFRESH_CACHE_BUST_VALUE\n"));
			const FString StillCached = AgentPromptLibrary::LoadAgentSection(
				EStationType::Sorter, TEXT("DefaultRule"));
			TestEqual(TEXT("without invalidate, cache still serves Content value"),
				StillCached, FromContent);

			// Now invalidate and re-read — should pick up Saved override.
			AgentPromptLibrary::InvalidateCache();
			const FString FreshlyLoaded = AgentPromptLibrary::LoadAgentSection(
				EStationType::Sorter, TEXT("DefaultRule"));
			TestEqual(TEXT("after invalidate, Saved override is read"),
				FreshlyLoaded, FString(TEXT("FRESH_CACHE_BUST_VALUE")));

			DeleteSavedAgent(EStationType::Sorter);
			AgentPromptLibrary::InvalidateCache();
		});
	});

	Describe("Checker DerivedRuleTemplate", [this]()
	{
		It("renders the upstream-rule composition when given the three rule placeholders", [this]()
		{
			const FString Tpl = AgentPromptLibrary::LoadAgentSection(
				EStationType::Checker, TEXT("DerivedRuleTemplate"));
			const FString Rendered = AgentPromptLibrary::FormatPrompt(Tpl, {
				{TEXT("generator_rule"), TEXT("Generate 5 ints")},
				{TEXT("filter_rule"),    TEXT("Keep evens")},
				{TEXT("sorter_rule"),    TEXT("Ascending")},
			});
			TestTrue(TEXT("Generator rule embedded"),
				Rendered.Contains(TEXT("Generate 5 ints")));
			TestTrue(TEXT("Filter rule embedded"),
				Rendered.Contains(TEXT("Keep evens")));
			TestTrue(TEXT("Sorter rule embedded"),
				Rendered.Contains(TEXT("Ascending")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
