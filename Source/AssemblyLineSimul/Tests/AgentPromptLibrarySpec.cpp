#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AgentPromptLibrary.h"
#include "AssemblyLineTypes.h"

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
		It("returns the Generator's DefaultRule from Generator.md verbatim", [this]()
		{
			const FString Rule = AgentPromptLibrary::LoadAgentSection(
				EStationType::Generator, TEXT("DefaultRule"));
			TestEqual(TEXT("Generator default rule"), Rule,
				FString(TEXT("Generate 10 random integers in the range 1 to 100")));
		});

		It("returns the Filter's Role description from Filter.md verbatim", [this]()
		{
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
