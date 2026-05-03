#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DAG/AssemblyLineDAG.h"
#include "DAG/DAGBuilder.h"
#include "AssemblyLineTypes.h"

namespace AssemblyLineDAGTests
{
	// Linear 4-node DAG matching today's hardcoded chain. The shape that
	// AC31a.1 (regression net) depends on.
	TArray<FStationNode> MakeLinearChain()
	{
		const FNodeRef G{EStationType::Generator, 0};
		const FNodeRef F{EStationType::Filter,    0};
		const FNodeRef S{EStationType::Sorter,    0};
		const FNodeRef C{EStationType::Checker,   0};
		return FDAGBuilder()
			.Source(G, TEXT("Generate"))
			.Edge(G, F, TEXT("Filter primes"))
			.Edge(F, S, TEXT("Ascending"))
			.Edge(S, C, TEXT("Verify"))
			.Build();
	}
}

DEFINE_SPEC(FAssemblyLineDAGSpec,
	"AssemblyLineSimul.AssemblyLineDAG",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FAssemblyLineDAGSpec::Define()
{
	using namespace AssemblyLineDAGTests;

	Describe("BuildFromDAG", [this]()
	{
		It("accepts the empty DAG (no nodes is a valid 0-node graph)", [this]()
		{
			FAssemblyLineDAG DAG;
			TestTrue(TEXT("empty build succeeds"), DAG.BuildFromDAG({}));
			TestEqual(TEXT("zero sources"),  DAG.GetSourceNodes().Num(), 0);
			TestEqual(TEXT("zero terminals"), DAG.GetTerminalNodes().Num(), 0);
		});

		It("accepts the linear 4-node chain", [this]()
		{
			FAssemblyLineDAG DAG;
			TestTrue(TEXT("linear build succeeds"), DAG.BuildFromDAG(MakeLinearChain()));
		});

		It("rejects a 2-node cycle (A->B->A) and logs an Error naming the cycle", [this]()
		{
			AddExpectedError(TEXT("cycle"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);

			const FNodeRef A{EStationType::Generator, 0};
			const FNodeRef B{EStationType::Filter,    0};
			const TArray<FStationNode> Cyclic = FDAGBuilder()
				.Edge(B, A)  // A's parent is B
				.Edge(A, B)  // B's parent is A — cycle
				.Build();

			FAssemblyLineDAG DAG;
			TestFalse(TEXT("cyclic build returns false"), DAG.BuildFromDAG(Cyclic));
		});

		It("rejects a 3-node cycle (A->B->C->A)", [this]()
		{
			AddExpectedError(TEXT("cycle"),
				EAutomationExpectedErrorFlags::Contains, /*ExpectedNumOccurrences=*/1);

			const FNodeRef A{EStationType::Generator, 0};
			const FNodeRef B{EStationType::Filter,    0};
			const FNodeRef C{EStationType::Sorter,    0};
			const TArray<FStationNode> Cyclic = FDAGBuilder()
				.Edge(C, A)  // A's parent is C
				.Edge(A, B)  // B's parent is A
				.Edge(B, C)  // C's parent is B — cycle
				.Build();

			FAssemblyLineDAG DAG;
			TestFalse(TEXT("3-cycle build returns false"), DAG.BuildFromDAG(Cyclic));
		});
	});

	Describe("GetSourceNodes / GetTerminalNodes", [this]()
	{
		It("on the linear chain: Generator is the only source, Checker the only terminal", [this]()
		{
			FAssemblyLineDAG DAG;
			DAG.BuildFromDAG(MakeLinearChain());

			const TArray<FNodeRef> Sources   = DAG.GetSourceNodes();
			const TArray<FNodeRef> Terminals = DAG.GetTerminalNodes();

			TestEqual(TEXT("one source"),   Sources.Num(),   1);
			TestEqual(TEXT("one terminal"), Terminals.Num(), 1);
			if (Sources.Num() == 1)
			{
				TestTrue(TEXT("source is Generator/0"),
					Sources[0] == FNodeRef{EStationType::Generator, 0});
			}
			if (Terminals.Num() == 1)
			{
				TestTrue(TEXT("terminal is Checker/0"),
					Terminals[0] == FNodeRef{EStationType::Checker, 0});
			}
		});

		It("on a fork (A -> B, A -> C): A is the source; B and C are terminals", [this]()
		{
			const FNodeRef A{EStationType::Generator, 0};
			const FNodeRef B{EStationType::Filter,    0};
			const FNodeRef C{EStationType::Sorter,    0};
			const TArray<FStationNode> Fork = FDAGBuilder()
				.Source(A).Edge(A, B).Edge(A, C).Build();

			FAssemblyLineDAG DAG;
			DAG.BuildFromDAG(Fork);

			TestEqual(TEXT("one source"),    DAG.GetSourceNodes().Num(),   1);
			TestEqual(TEXT("two terminals"), DAG.GetTerminalNodes().Num(), 2);
		});

		It("on a merge (A -> C, B -> C): A and B are sources; C is the terminal", [this]()
		{
			const FNodeRef A{EStationType::Generator, 0};
			const FNodeRef B{EStationType::Filter,    0};
			const FNodeRef C{EStationType::Sorter,    0};
			const TArray<FStationNode> Merge = FDAGBuilder()
				.Source(A).Source(B).Edge(A, C).Edge(B, C).Build();

			FAssemblyLineDAG DAG;
			DAG.BuildFromDAG(Merge);

			TestEqual(TEXT("two sources"),  DAG.GetSourceNodes().Num(),   2);
			TestEqual(TEXT("one terminal"), DAG.GetTerminalNodes().Num(), 1);
		});
	});

	Describe("GetAncestors", [this]()
	{
		It("on the linear chain: Checker's ancestors are {Sorter, Filter, Generator}", [this]()
		{
			FAssemblyLineDAG DAG;
			DAG.BuildFromDAG(MakeLinearChain());

			const TArray<FNodeRef> Ancestors = DAG.GetAncestors(
				FNodeRef{EStationType::Checker, 0});
			TestEqual(TEXT("three ancestors"), Ancestors.Num(), 3);
			TestTrue(TEXT("includes Sorter"),
				Ancestors.Contains(FNodeRef{EStationType::Sorter, 0}));
			TestTrue(TEXT("includes Filter"),
				Ancestors.Contains(FNodeRef{EStationType::Filter, 0}));
			TestTrue(TEXT("includes Generator"),
				Ancestors.Contains(FNodeRef{EStationType::Generator, 0}));
		});

		It("on a fork-merge (A -> B, A -> C, B -> D, C -> D): D's ancestors are {B, C, A}", [this]()
		{
			const FNodeRef A{EStationType::Generator, 0};
			const FNodeRef B{EStationType::Filter,    0};
			const FNodeRef C{EStationType::Sorter,    0};
			const FNodeRef D{EStationType::Checker,   0};
			const TArray<FStationNode> ForkMerge = FDAGBuilder()
				.Source(A).Edge(A, B).Edge(A, C).Edge(B, D).Edge(C, D).Build();
			FAssemblyLineDAG DAG;
			DAG.BuildFromDAG(ForkMerge);

			const TArray<FNodeRef> Ancestors = DAG.GetAncestors(
				FNodeRef{EStationType::Checker, 0});
			TestEqual(TEXT("three unique ancestors (A counted once via dedup)"),
				Ancestors.Num(), 3);
			TestTrue(TEXT("includes A"), Ancestors.Contains(A));
			TestTrue(TEXT("includes B"), Ancestors.Contains(B));
			TestTrue(TEXT("includes C"), Ancestors.Contains(C));
		});

		It("on a source node: ancestors is empty", [this]()
		{
			FAssemblyLineDAG DAG;
			DAG.BuildFromDAG(MakeLinearChain());

			const TArray<FNodeRef> Ancestors = DAG.GetAncestors(
				FNodeRef{EStationType::Generator, 0});
			TestEqual(TEXT("source has no ancestors"), Ancestors.Num(), 0);
		});
	});

	Describe("GetSuccessors", [this]()
	{
		It("on the linear chain: Filter's successor is Sorter", [this]()
		{
			FAssemblyLineDAG DAG;
			DAG.BuildFromDAG(MakeLinearChain());

			const TArray<FNodeRef> Succ = DAG.GetSuccessors(
				FNodeRef{EStationType::Filter, 0});
			TestEqual(TEXT("one successor"), Succ.Num(), 1);
			if (Succ.Num() == 1)
			{
				TestTrue(TEXT("successor is Sorter/0"),
					Succ[0] == FNodeRef{EStationType::Sorter, 0});
			}
		});

		It("on the terminal: no successors", [this]()
		{
			FAssemblyLineDAG DAG;
			DAG.BuildFromDAG(MakeLinearChain());

			const TArray<FNodeRef> Succ = DAG.GetSuccessors(
				FNodeRef{EStationType::Checker, 0});
			TestEqual(TEXT("terminal has no successors"), Succ.Num(), 0);
		});

		It("on a fork: Generator has both Filter and Sorter as successors", [this]()
		{
			const FNodeRef A{EStationType::Generator, 0};
			const FNodeRef B{EStationType::Filter,    0};
			const FNodeRef C{EStationType::Sorter,    0};
			const TArray<FStationNode> Fork = FDAGBuilder()
				.Source(A).Edge(A, B).Edge(A, C).Build();
			FAssemblyLineDAG DAG;
			DAG.BuildFromDAG(Fork);

			const TArray<FNodeRef> Succ = DAG.GetSuccessors(A);
			TestEqual(TEXT("two successors"), Succ.Num(), 2);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
