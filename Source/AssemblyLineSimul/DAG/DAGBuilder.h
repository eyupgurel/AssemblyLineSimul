#pragma once

#include "CoreMinimal.h"
#include "DAG/AssemblyLineDAG.h"

// Story 31e — fluent builder for FStationNode arrays. Use in tests
// (and Story 32's orchestrator-output specs) to express DAG fixtures
// in a few lines that read like the diagram:
//
//   const TArray<FStationNode> Spec = FDAGBuilder()
//       .Source({Generator, 0}, TEXT("gen"))
//       .Edge({Generator, 0}, {Filter, 0}, TEXT("filter"))
//       .Edge({Filter,    0}, {Sorter, 0}, TEXT("sort"))
//       .Edge({Sorter,    0}, {Checker, 0})
//       .Build();
//
// Header-only because each method is a few lines of TMap manipulation.
class FDAGBuilder
{
public:
	// Register a parent-less node. Subsequent Edge() calls may add
	// parents or be Edge(From, ThisRef) to make this node a child.
	FDAGBuilder& Source(const FNodeRef& Ref, const FString& Rule = FString())
	{
		FStationNode& N = GetOrAdd(Ref);
		N.Rule = Rule;
		return *this;
	}

	// Add an edge From -> To. To is auto-created if it hasn't been
	// declared yet. ToRule, when non-empty, overwrites To's Rule.
	// Duplicate Edge(From, To) calls are deduplicated (AddUnique).
	FDAGBuilder& Edge(const FNodeRef& From, const FNodeRef& To, const FString& ToRule = FString())
	{
		// Touch From so it shows up in InsertionOrder even if no
		// Source(From) was called explicitly.
		(void)GetOrAdd(From);
		FStationNode& ToNode = GetOrAdd(To);
		ToNode.Parents.AddUnique(From);
		if (!ToRule.IsEmpty())
		{
			ToNode.Rule = ToRule;
		}
		return *this;
	}

	// Emit the accumulated nodes in insertion order.
	TArray<FStationNode> Build() const
	{
		TArray<FStationNode> Out;
		Out.Reserve(InsertionOrder.Num());
		for (const FNodeRef& Ref : InsertionOrder)
		{
			if (const FStationNode* N = Nodes.Find(Ref))
			{
				Out.Add(*N);
			}
		}
		return Out;
	}

private:
	FStationNode& GetOrAdd(const FNodeRef& Ref)
	{
		if (FStationNode* Existing = Nodes.Find(Ref))
		{
			return *Existing;
		}
		InsertionOrder.Add(Ref);
		FStationNode New;
		New.Ref = Ref;
		return Nodes.Add(Ref, New);
	}

	TArray<FNodeRef>           InsertionOrder;
	TMap<FNodeRef, FStationNode> Nodes;
};
