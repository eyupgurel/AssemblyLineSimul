#include "DAG/AssemblyLineDAG.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssemblyLineDAG, Log, All);

void FAssemblyLineDAG::Reset()
{
	Nodes.Reset();
	RefToIndex.Reset();
	bSuccessorCacheBuilt = false;
	SuccessorCache.Reset();
}

bool FAssemblyLineDAG::BuildFromDAG(const TArray<FStationNode>& InNodes)
{
	Reset();

	// Stage 1 — register every node, reject duplicate refs.
	for (const FStationNode& N : InNodes)
	{
		if (RefToIndex.Contains(N.Ref))
		{
			UE_LOG(LogAssemblyLineDAG, Error,
				TEXT("BuildFromDAG: duplicate NodeRef Kind=%d Instance=%d"),
				static_cast<int32>(N.Ref.Kind), N.Ref.Instance);
			Reset();
			return false;
		}
		RefToIndex.Add(N.Ref, Nodes.Num());
		Nodes.Add(MakeShared<const FStationNode>(N));
	}

	// Stage 2 — every parent ref must resolve to a known node.
	for (const TSharedRef<const FStationNode>& Node : Nodes)
	{
		for (const FNodeRef& Parent : Node->Parents)
		{
			if (!RefToIndex.Contains(Parent))
			{
				UE_LOG(LogAssemblyLineDAG, Error,
					TEXT("BuildFromDAG: node Kind=%d Instance=%d references unknown parent Kind=%d Instance=%d"),
					static_cast<int32>(Node->Ref.Kind), Node->Ref.Instance,
					static_cast<int32>(Parent.Kind), Parent.Instance);
				Reset();
				return false;
			}
		}
	}

	// Stage 3 — Kahn's algorithm cycle detection.
	// in-degree[N] = number of unprocessed parents of N
	TArray<int32> InDegree;
	InDegree.SetNumZeroed(Nodes.Num());
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		InDegree[i] = Nodes[i]->Parents.Num();
	}

	TArray<int32> Ready;
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		if (InDegree[i] == 0) Ready.Add(i);
	}

	// Pre-compute children for the Kahn loop only (not stored — successor
	// cache is built separately on first query).
	TMap<FNodeRef, TArray<int32>> ChildIndices;
	ChildIndices.Reserve(Nodes.Num());
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		for (const FNodeRef& Parent : Nodes[i]->Parents)
		{
			ChildIndices.FindOrAdd(Parent).Add(i);
		}
	}

	int32 Processed = 0;
	while (Ready.Num() > 0)
	{
		const int32 Idx = Ready.Pop(EAllowShrinking::No);
		++Processed;
		if (TArray<int32>* Children = ChildIndices.Find(Nodes[Idx]->Ref))
		{
			for (int32 ChildIdx : *Children)
			{
				if (--InDegree[ChildIdx] == 0)
				{
					Ready.Add(ChildIdx);
				}
			}
		}
	}

	if (Processed != Nodes.Num())
	{
		// Surviving nodes (InDegree > 0) form the cycle. Name a few in the log.
		FString CycleMembers;
		for (int32 i = 0; i < Nodes.Num() && CycleMembers.Len() < 200; ++i)
		{
			if (InDegree[i] > 0)
			{
				CycleMembers += FString::Printf(TEXT("(Kind=%d,Inst=%d) "),
					static_cast<int32>(Nodes[i]->Ref.Kind), Nodes[i]->Ref.Instance);
			}
		}
		UE_LOG(LogAssemblyLineDAG, Error,
			TEXT("BuildFromDAG: cycle detected — %d nodes left after Kahn drain: %s"),
			Nodes.Num() - Processed, *CycleMembers);
		Reset();
		return false;
	}

	return true;
}

const FStationNode* FAssemblyLineDAG::FindNode(const FNodeRef& Node) const
{
	if (const int32* Idx = RefToIndex.Find(Node))
	{
		return &Nodes[*Idx].Get();
	}
	return nullptr;
}

TArray<FNodeRef> FAssemblyLineDAG::GetParents(const FNodeRef& Node) const
{
	if (const FStationNode* N = FindNode(Node))
	{
		return N->Parents;
	}
	return {};
}

void FAssemblyLineDAG::EnsureSuccessorCache() const
{
	if (bSuccessorCacheBuilt) return;
	for (const TSharedRef<const FStationNode>& N : Nodes)
	{
		// Touch the entry so terminals (no successors) still appear in queries
		// without a separate empty-array allocation per call.
		SuccessorCache.FindOrAdd(N->Ref);
	}
	for (const TSharedRef<const FStationNode>& N : Nodes)
	{
		for (const FNodeRef& Parent : N->Parents)
		{
			SuccessorCache.FindOrAdd(Parent).Add(N->Ref);
		}
	}
	bSuccessorCacheBuilt = true;
}

TArray<FNodeRef> FAssemblyLineDAG::GetSuccessors(const FNodeRef& Node) const
{
	EnsureSuccessorCache();
	if (const TArray<FNodeRef>* Found = SuccessorCache.Find(Node))
	{
		return *Found;
	}
	return {};
}

TArray<FNodeRef> FAssemblyLineDAG::GetSourceNodes() const
{
	TArray<FNodeRef> Out;
	for (const TSharedRef<const FStationNode>& N : Nodes)
	{
		if (N->Parents.Num() == 0) Out.Add(N->Ref);
	}
	return Out;
}

TArray<FNodeRef> FAssemblyLineDAG::GetTerminalNodes() const
{
	EnsureSuccessorCache();
	TArray<FNodeRef> Out;
	for (const TSharedRef<const FStationNode>& N : Nodes)
	{
		const TArray<FNodeRef>* Succ = SuccessorCache.Find(N->Ref);
		if (!Succ || Succ->Num() == 0)
		{
			Out.Add(N->Ref);
		}
	}
	return Out;
}

TArray<FNodeRef> FAssemblyLineDAG::GetAncestors(const FNodeRef& Node) const
{
	// Iterative BFS with a Visited set — Sui's pattern (no recursion,
	// dedup naturally handled). Excludes Node itself.
	TArray<FNodeRef> Out;
	TSet<FNodeRef> Visited;
	Visited.Add(Node);  // prevent self-inclusion in fork-merge cases

	TArray<FNodeRef> Queue = GetParents(Node);
	while (Queue.Num() > 0)
	{
		const FNodeRef N = Queue.Pop(EAllowShrinking::No);
		if (Visited.Contains(N)) continue;
		Visited.Add(N);
		Out.Add(N);
		for (const FNodeRef& P : GetParents(N))
		{
			if (!Visited.Contains(P)) Queue.Add(P);
		}
	}
	return Out;
}
