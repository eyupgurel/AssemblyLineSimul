#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyLineTypes.h"
#include "VoiceHailParser.h"

DEFINE_SPEC(FVoiceHailParserSpec,
	"AssemblyLineSimul.VoiceHailParser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FVoiceHailParserSpec::Define()
{
	using namespace AssemblyLineVoice;

	Describe("TryParseHail", [this]()
	{
		It("matches the canonical 'hey <agent> do you read me' pattern for all four agents", [this]()
		{
			EStationType S = EStationType::Generator;
			TestTrue(TEXT("hails Generator"),
				TryParseHail(TEXT("hey generator do you read me"), S));
			TestEqual(TEXT("Generator extracted"), S, EStationType::Generator);

			TestTrue(TEXT("hails Filter"),
				TryParseHail(TEXT("hey filter do you read me"), S));
			TestEqual(TEXT("Filter extracted"), S, EStationType::Filter);

			TestTrue(TEXT("hails Sorter"),
				TryParseHail(TEXT("hey sorter do you read me"), S));
			TestEqual(TEXT("Sorter extracted"), S, EStationType::Sorter);

			TestTrue(TEXT("hails Checker"),
				TryParseHail(TEXT("hey checker do you read me"), S));
			TestEqual(TEXT("Checker extracted"), S, EStationType::Checker);
		});

		It("is case-insensitive and tolerates punctuation", [this]()
		{
			EStationType S = EStationType::Generator;
			TestTrue(TEXT("punctuation OK"),
				TryParseHail(TEXT("Hey, Filter — do you read me?"), S));
			TestEqual(TEXT("Filter extracted"), S, EStationType::Filter);

			TestTrue(TEXT("uppercase OK"),
				TryParseHail(TEXT("HEY SORTER DO YOU READ ME"), S));
			TestEqual(TEXT("Sorter extracted"), S, EStationType::Sorter);
		});

		It("accepts alternative confirmation phrases", [this]()
		{
			EStationType S = EStationType::Generator;
			TestTrue(TEXT("'do you copy' works"),
				TryParseHail(TEXT("hey checker do you copy"), S));
			TestEqual(TEXT("Checker extracted"), S, EStationType::Checker);

			TestTrue(TEXT("'are you there' works"),
				TryParseHail(TEXT("hey generator are you there"), S));
			TestEqual(TEXT("Generator extracted"), S, EStationType::Generator);
		});

		It("rejects utterances that aren't hails", [this]()
		{
			EStationType S = EStationType::Generator;
			TestFalse(TEXT("plain command rejected"),
				TryParseHail(TEXT("filter only odd numbers"), S));
			TestFalse(TEXT("non-agent name rejected"),
				TryParseHail(TEXT("hey people do you read me"), S));
			TestFalse(TEXT("empty rejected"),
				TryParseHail(TEXT(""), S));
			TestFalse(TEXT("hey-without-agent rejected"),
				TryParseHail(TEXT("hey there do you read me"), S));
		});

		It("tolerates close misspellings of the agent name (Whisper sometimes mishears)", [this]()
		{
			EStationType S = EStationType::Generator;
			TestTrue(TEXT("'filtre' counts as Filter"),
				TryParseHail(TEXT("hey filtre do you read me"), S));
			TestEqual(TEXT("Filter extracted"), S, EStationType::Filter);

			TestTrue(TEXT("'soter' counts as Sorter"),
				TryParseHail(TEXT("hey soter do you read me"), S));
			TestEqual(TEXT("Sorter extracted"), S, EStationType::Sorter);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
