#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DAG/AssemblyLineDAG.h"
#include "DAG/DAGBuilder.h"
#include "AssemblyLineTypes.h"

DEFINE_SPEC(FDAGBuilderSpec,
	"AssemblyLineSimul.DAGBuilder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FDAGBuilderSpec::Define()
{
	const FNodeRef G{EStationType::Generator, 0};
	const FNodeRef F{EStationType::Filter,    0};
	const FNodeRef S{EStationType::Sorter,    0};
	const FNodeRef C{EStationType::Checker,   0};

	Describe("Build", [this, G, F]()
	{
		It("returns an empty array when no calls have been made", [this]()
		{
			FDAGBuilder B;
			TestEqual(TEXT("empty Build"), B.Build().Num(), 0);
		});

		It("is deterministic: two consecutive Build() calls produce identical output", [this, G, F]()
		{
			FDAGBuilder B;
			B.Source(G).Edge(G, F);
			const TArray<FStationNode> First  = B.Build();
			const TArray<FStationNode> Second = B.Build();
			TestEqual(TEXT("same node count"), First.Num(), Second.Num());
			if (First.Num() == Second.Num())
			{
				for (int32 i = 0; i < First.Num(); ++i)
				{
					TestTrue(*FString::Printf(TEXT("same Ref at %d"), i),
						First[i].Ref == Second[i].Ref);
				}
			}
		});
	});

	Describe("Source", [this, G]()
	{
		It("registers a parent-less node with the supplied Rule", [this, G]()
		{
			FDAGBuilder B;
			B.Source(G, TEXT("Generate 10 ints"));
			const TArray<FStationNode> Result = B.Build();
			TestEqual(TEXT("one node"), Result.Num(), 1);
			if (Result.Num() == 1)
			{
				TestTrue(TEXT("Ref matches"), Result[0].Ref == G);
				TestEqual(TEXT("Rule matches"), Result[0].Rule, FString(TEXT("Generate 10 ints")));
				TestEqual(TEXT("no parents"), Result[0].Parents.Num(), 0);
			}
		});
	});

	Describe("Edge", [this, G, F, S]()
	{
		It("auto-creates the To node if not previously declared", [this, G, F]()
		{
			FDAGBuilder B;
			B.Source(G).Edge(G, F, TEXT("Keep primes"));
			const TArray<FStationNode> Result = B.Build();
			TestEqual(TEXT("two nodes"), Result.Num(), 2);
			if (Result.Num() == 2)
			{
				// Insertion order: G first (Source), F second (Edge auto-created).
				TestTrue(TEXT("[0] is G"), Result[0].Ref == G);
				TestTrue(TEXT("[1] is F"), Result[1].Ref == F);
				TestEqual(TEXT("F has 1 parent"), Result[1].Parents.Num(), 1);
				TestTrue(TEXT("F's parent is G"), Result[1].Parents[0] == G);
				TestEqual(TEXT("F's Rule was set by Edge"),
					Result[1].Rule, FString(TEXT("Keep primes")));
			}
		});

		It("accumulates parents on the To node when called multiple times — fan-in", [this, G, F, S]()
		{
			FDAGBuilder B;
			B.Source(G).Source(F).Edge(G, S).Edge(F, S);
			const TArray<FStationNode> Result = B.Build();
			TestEqual(TEXT("three nodes"), Result.Num(), 3);

			const FStationNode* SNode = Result.FindByPredicate(
				[&S](const FStationNode& N) { return N.Ref == S; });
			TestNotNull(TEXT("S node present"), SNode);
			if (SNode)
			{
				TestEqual(TEXT("S has 2 parents"), SNode->Parents.Num(), 2);
				TestTrue(TEXT("S parent G"), SNode->Parents.Contains(G));
				TestTrue(TEXT("S parent F"), SNode->Parents.Contains(F));
			}
		});

		It("expresses fan-out when same From has multiple Edge calls — From-Edge-To-different-Tos", [this, G, F, S]()
		{
			FDAGBuilder B;
			B.Source(G).Edge(G, F).Edge(G, S);
			const TArray<FStationNode> Result = B.Build();
			TestEqual(TEXT("three nodes"), Result.Num(), 3);

			const FStationNode* FNode = Result.FindByPredicate(
				[&F](const FStationNode& N) { return N.Ref == F; });
			const FStationNode* SNode = Result.FindByPredicate(
				[&S](const FStationNode& N) { return N.Ref == S; });
			TestNotNull(TEXT("F present"), FNode);
			TestNotNull(TEXT("S present"), SNode);
			if (FNode) TestTrue(TEXT("F's parent is G"),
				FNode->Parents.Num() == 1 && FNode->Parents[0] == G);
			if (SNode) TestTrue(TEXT("S's parent is G"),
				SNode->Parents.Num() == 1 && SNode->Parents[0] == G);
		});

		It("dedupes a duplicate Edge(A, B) call — no double-parent", [this, G, F]()
		{
			FDAGBuilder B;
			B.Source(G).Edge(G, F).Edge(G, F);
			const TArray<FStationNode> Result = B.Build();
			const FStationNode* FNode = Result.FindByPredicate(
				[&F](const FStationNode& N) { return N.Ref == F; });
			TestNotNull(TEXT("F present"), FNode);
			if (FNode)
			{
				TestEqual(TEXT("F has exactly one parent (G), not two"),
					FNode->Parents.Num(), 1);
			}
		});
	});

	Describe("End-to-end: feeds FAssemblyLineDAG", [this, G, F, S, C]()
	{
		It("a builder-produced linear chain BuildFromDAG'd succeeds", [this, G, F, S, C]()
		{
			FDAGBuilder B;
			const TArray<FStationNode> Spec = B
				.Source(G, TEXT("gen"))
				.Edge(G, F, TEXT("filter"))
				.Edge(F, S, TEXT("sort"))
				.Edge(S, C, TEXT("check"))
				.Build();

			FAssemblyLineDAG DAG;
			TestTrue(TEXT("BuildFromDAG accepts builder output"), DAG.BuildFromDAG(Spec));
			TestEqual(TEXT("4 nodes"), DAG.NumNodes(), 4);
			TestEqual(TEXT("one source"),   DAG.GetSourceNodes().Num(),   1);
			TestEqual(TEXT("one terminal"), DAG.GetTerminalNodes().Num(), 1);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
