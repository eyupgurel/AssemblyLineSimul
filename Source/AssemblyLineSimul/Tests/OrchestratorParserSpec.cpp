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

	Describe("ParsePlan (Story 33b — full reply with dag + prompts)", [this]()
	{
		It("extracts the prompts object alongside dag", [this]()
		{
			const FString FullReply = TEXT(
				"{\"reply\":\"sure thing\","
				"\"dag\":{\"nodes\":["
				"{\"id\":\"g\",\"type\":\"Generator\",\"rule\":\"r1\"},"
				"{\"id\":\"f\",\"type\":\"Filter\",\"rule\":\"r2\",\"parents\":[\"g\"]}"
				"]},"
				"\"prompts\":{"
				"\"Generator\":\"You are the source of fresh batches.\","
				"\"Filter\":\"You sift the wheat from the chaff.\""
				"}}");
			TArray<FStationNode> Nodes;
			TMap<EStationType, FString> Prompts;
			const bool bOk = OrchestratorParser::ParsePlan(FullReply, Nodes, Prompts);
			TestTrue(TEXT("ParsePlan succeeded"), bOk);
			TestEqual(TEXT("2 nodes parsed"), Nodes.Num(), 2);
			TestEqual(TEXT("2 prompts parsed"), Prompts.Num(), 2);
			TestTrue(TEXT("Generator prompt prose preserved"),
				Prompts.FindRef(EStationType::Generator).Contains(TEXT("source of fresh batches")));
			TestTrue(TEXT("Filter prompt prose preserved"),
				Prompts.FindRef(EStationType::Filter).Contains(TEXT("wheat from the chaff")));
		});

		It("returns empty prompts map when the prompts field is absent (non-fatal)", [this]()
		{
			const FString DagOnly = TEXT(
				"{\"reply\":\"sure\","
				"\"dag\":{\"nodes\":[{\"id\":\"g\",\"type\":\"Generator\",\"rule\":\"\"}]}}");
			TArray<FStationNode> Nodes;
			TMap<EStationType, FString> Prompts;
			const bool bOk = OrchestratorParser::ParsePlan(DagOnly, Nodes, Prompts);
			TestTrue(TEXT("missing prompts is non-fatal"), bOk);
			TestEqual(TEXT("1 node parsed"), Nodes.Num(), 1);
			TestEqual(TEXT("0 prompts"), Prompts.Num(), 0);
		});

		It("logs Warning and skips entries with unknown station-type keys", [this]()
		{
			AddExpectedError(TEXT("unknown station type"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);

			const FString WithBadKey = TEXT(
				"{\"reply\":\"\","
				"\"dag\":{\"nodes\":[{\"id\":\"g\",\"type\":\"Generator\",\"rule\":\"\"}]},"
				"\"prompts\":{\"Generator\":\"valid\",\"FooBar\":\"skipped\"}}");
			TArray<FStationNode> Nodes;
			TMap<EStationType, FString> Prompts;
			const bool bOk = OrchestratorParser::ParsePlan(WithBadKey, Nodes, Prompts);
			TestTrue(TEXT("ParsePlan still succeeds with unknown prompt key"), bOk);
			TestEqual(TEXT("only the valid Generator entry kept"), Prompts.Num(), 1);
			TestTrue(TEXT("Generator entry preserved"),
				Prompts.Contains(EStationType::Generator));
		});

		It("returns false on malformed JSON (delegates dag failure)", [this]()
		{
			AddExpectedError(TEXT("ParsePlan"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);
			TArray<FStationNode> Nodes;
			TMap<EStationType, FString> Prompts;
			const bool bOk = OrchestratorParser::ParsePlan(
				TEXT("not even json"), Nodes, Prompts);
			TestFalse(TEXT("malformed returns false"), bOk);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
