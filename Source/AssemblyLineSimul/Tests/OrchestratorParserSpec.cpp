#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DAG/OrchestratorParser.h"
#include "DAG/AssemblyLineDAG.h"
#include "AssemblyLineTypes.h"

DEFINE_SPEC(FOrchestratorParserSpec,
	"AssemblyLineSimul.OrchestratorParser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FOrchestratorParserSpec::Define()
{
	Describe("ParseDAGSpec", [this]()
	{
		It("accepts the empty spec {\"nodes\":[]} and returns an empty TArray", [this]()
		{
			TArray<FStationNode> Out;
			const bool bOk = OrchestratorParser::ParseDAGSpec(
				TEXT("{\"nodes\":[]}"), Out);
			TestTrue(TEXT("empty spec parses"), bOk);
			TestEqual(TEXT("zero nodes"), Out.Num(), 0);
		});

		It("parses a 4-node linear chain (single instance per kind) into the expected FStationNodes", [this]()
		{
			const FString Spec = TEXT(
				"{\"nodes\":["
				"{\"id\":\"gen\",\"type\":\"Generator\",\"rule\":\"gen-rule\"},"
				"{\"id\":\"flt\",\"type\":\"Filter\",\"rule\":\"flt-rule\",\"parents\":[\"gen\"]},"
				"{\"id\":\"srt\",\"type\":\"Sorter\",\"rule\":\"srt-rule\",\"parents\":[\"flt\"]},"
				"{\"id\":\"chk\",\"type\":\"Checker\",\"rule\":\"chk-rule\",\"parents\":[\"srt\"]}"
				"]}");
			TArray<FStationNode> Out;
			TestTrue(TEXT("linear parses"), OrchestratorParser::ParseDAGSpec(Spec, Out));
			TestEqual(TEXT("4 nodes"), Out.Num(), 4);
			if (Out.Num() != 4) return;

			TestTrue(TEXT("[0] is Generator/0"),
				Out[0].Ref == FNodeRef{EStationType::Generator, 0});
			TestEqual(TEXT("[0] rule"), Out[0].Rule, FString(TEXT("gen-rule")));
			TestEqual(TEXT("[0] no parents"), Out[0].Parents.Num(), 0);

			TestTrue(TEXT("[1] is Filter/0"),
				Out[1].Ref == FNodeRef{EStationType::Filter, 0});
			TestEqual(TEXT("[1] one parent"), Out[1].Parents.Num(), 1);
			if (Out[1].Parents.Num() == 1)
			{
				TestTrue(TEXT("[1] parent is Generator/0"),
					Out[1].Parents[0] == FNodeRef{EStationType::Generator, 0});
			}

			TestTrue(TEXT("[3] is Checker/0"),
				Out[3].Ref == FNodeRef{EStationType::Checker, 0});
			TestEqual(TEXT("[3] rule"), Out[3].Rule, FString(TEXT("chk-rule")));
		});

		It("parses a fan-out spec (one source -> two children)", [this]()
		{
			const FString Spec = TEXT(
				"{\"nodes\":["
				"{\"id\":\"src\",\"type\":\"Generator\",\"rule\":\"\"},"
				"{\"id\":\"a\",\"type\":\"Filter\",\"rule\":\"\",\"parents\":[\"src\"]},"
				"{\"id\":\"b\",\"type\":\"Sorter\",\"rule\":\"\",\"parents\":[\"src\"]}"
				"]}");
			TArray<FStationNode> Out;
			TestTrue(TEXT("fan-out parses"), OrchestratorParser::ParseDAGSpec(Spec, Out));
			TestEqual(TEXT("3 nodes"), Out.Num(), 3);

			// Both Filter and Sorter should list Generator as parent.
			const FStationNode* FlNode = Out.FindByPredicate(
				[](const FStationNode& N) { return N.Ref.Kind == EStationType::Filter; });
			const FStationNode* SrNode = Out.FindByPredicate(
				[](const FStationNode& N) { return N.Ref.Kind == EStationType::Sorter; });
			TestNotNull(TEXT("Filter present"), FlNode);
			TestNotNull(TEXT("Sorter present"), SrNode);
			if (FlNode) TestTrue(TEXT("Filter parent is Generator"),
				FlNode->Parents.Num() == 1 &&
				FlNode->Parents[0] == FNodeRef{EStationType::Generator, 0});
			if (SrNode) TestTrue(TEXT("Sorter parent is Generator"),
				SrNode->Parents.Num() == 1 &&
				SrNode->Parents[0] == FNodeRef{EStationType::Generator, 0});
		});

		It("parses a fan-in spec (two sources -> one merge child)", [this]()
		{
			const FString Spec = TEXT(
				"{\"nodes\":["
				"{\"id\":\"a\",\"type\":\"Generator\",\"rule\":\"\"},"
				"{\"id\":\"b\",\"type\":\"Filter\",\"rule\":\"\"},"
				"{\"id\":\"merge\",\"type\":\"Sorter\",\"rule\":\"\",\"parents\":[\"a\",\"b\"]}"
				"]}");
			TArray<FStationNode> Out;
			TestTrue(TEXT("fan-in parses"), OrchestratorParser::ParseDAGSpec(Spec, Out));
			TestEqual(TEXT("3 nodes"), Out.Num(), 3);

			const FStationNode* MergeNode = Out.FindByPredicate(
				[](const FStationNode& N) { return N.Ref.Kind == EStationType::Sorter; });
			TestNotNull(TEXT("merge node present"), MergeNode);
			if (MergeNode)
			{
				TestEqual(TEXT("merge has 2 parents"), MergeNode->Parents.Num(), 2);
			}
		});

		It("returns false on malformed JSON", [this]()
		{
			AddExpectedError(TEXT("ParseDAGSpec"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);
			TArray<FStationNode> Out;
			const bool bOk = OrchestratorParser::ParseDAGSpec(
				TEXT("not even json"), Out);
			TestFalse(TEXT("malformed returns false"), bOk);
		});

		It("returns false on unknown station type", [this]()
		{
			AddExpectedError(TEXT("ParseDAGSpec"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);
			TArray<FStationNode> Out;
			const bool bOk = OrchestratorParser::ParseDAGSpec(
				TEXT("{\"nodes\":[{\"id\":\"x\",\"type\":\"FooBar\",\"rule\":\"\"}]}"), Out);
			TestFalse(TEXT("unknown type returns false"), bOk);
		});

		It("returns false when a node references an undeclared parent ID", [this]()
		{
			AddExpectedError(TEXT("ParseDAGSpec"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);
			TArray<FStationNode> Out;
			const bool bOk = OrchestratorParser::ParseDAGSpec(
				TEXT("{\"nodes\":["
					 "{\"id\":\"a\",\"type\":\"Filter\",\"rule\":\"\",\"parents\":[\"missing\"]}"
					 "]}"), Out);
			TestFalse(TEXT("undeclared-parent returns false"), bOk);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
