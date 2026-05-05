#pragma once

#include "CoreMinimal.h"
#include "AssemblyLineTypes.h"
#include "AssemblyLineDAG.generated.h"

// Layer 1 — typed identity. (Kind, Instance) is range-scannable in any
// TMap<FNodeRef, ...> — useful for "all Filter nodes" debug queries.
// See Docs/DAG_Architecture.md.
//
// Story 35 — promoted to USTRUCT so UPROPERTY TMap<FNodeRef, ...> on
// UAssemblyLineDirector reflects through UHT. The original "pure-domain"
// claim in the architecture doc is partially aspirational; we already
// pull in CoreMinimal types (TArray, TMap, TWeakObjectPtr<APayloadCarrier>) so
// USTRUCT is just another flavor of that.
USTRUCT()
struct ASSEMBLYLINESIMUL_API FNodeRef
{
	GENERATED_BODY()

	UPROPERTY()
	EStationType Kind = EStationType::Generator;

	UPROPERTY()
	int32 Instance = 0;

	bool operator==(const FNodeRef& Other) const
	{
		return Kind == Other.Kind && Instance == Other.Instance;
	}
	bool operator!=(const FNodeRef& Other) const { return !(*this == Other); }
	bool operator<(const FNodeRef& Other) const
	{
		if (Kind != Other.Kind) return static_cast<uint8>(Kind) < static_cast<uint8>(Other.Kind);
		return Instance < Other.Instance;
	}
};

FORCEINLINE uint32 GetTypeHash(const FNodeRef& Ref)
{
	return HashCombine(::GetTypeHash(static_cast<uint8>(Ref.Kind)),
	                    ::GetTypeHash(Ref.Instance));
}

// Immutable after BuildFromDAG (held in TSharedRef inside the graph so
// readers get cheap pointer copies). Edges are on the child only — the
// parent doesn't know its children; back-edges are computed lazily by
// FAssemblyLineDAG::GetSuccessors.
struct FStationNode
{
	FNodeRef         Ref;
	FString          Rule;     // EffectiveRule for the station (Generator / Filter / Sorter)
	TArray<FNodeRef> Parents;  // forward edges, immutable
};

// Mutable per-node side-band. Keyed by FNodeRef in FAssemblyLineDAG.
// Future stories (31c/31d/31e) will populate InboundBuckets, GcWatermark, etc.
struct FStationNodeState
{
	bool                              bProcessed = false;
	TArray<TWeakObjectPtr<class APayloadCarrier>> InboundBuckets;
	int32                             GcWatermark = 0;
};

// Pure-domain DAG. No engine dependencies beyond CoreMinimal + the
// project's EStationType enum (per Layer-1 dependency rule in
// Docs/DAG_Architecture.md). Held by value on UAssemblyLineDirector.
class ASSEMBLYLINESIMUL_API FAssemblyLineDAG
{
public:
	// Validates (Kahn's cycle check, duplicate-NodeRef check) and ingests
	// the spec. Returns false + logs Error on failure; on failure the
	// instance is left empty (callers must not call query methods).
	bool BuildFromDAG(const TArray<FStationNode>& InNodes);

	// Forward edges (immutable, stored on the node).
	TArray<FNodeRef> GetParents(const FNodeRef& Node) const;

	// Back edges. Computed lazily on first call — Sui's pattern.
	TArray<FNodeRef> GetSuccessors(const FNodeRef& Node) const;

	// Topological queries. Deterministic ordering (TArray result follows
	// insertion order from BuildFromDAG, with no reshuffling).
	TArray<FNodeRef> GetSourceNodes() const;
	TArray<FNodeRef> GetTerminalNodes() const;

	// Iterative BFS, no recursion. Excludes Node itself.
	TArray<FNodeRef> GetAncestors(const FNodeRef& Node) const;

	// Lookup. Returns nullptr if Node isn't in the graph.
	const FStationNode* FindNode(const FNodeRef& Node) const;

	int32 NumNodes() const { return Nodes.Num(); }

private:
	// Insertion-order-preserving registry of nodes. We use TArray (not TMap)
	// for the primary store so GetSourceNodes / GetTerminalNodes return
	// deterministic order matching the BuildFromDAG input.
	TArray<TSharedRef<const FStationNode>> Nodes;
	TMap<FNodeRef, int32> RefToIndex;  // O(1) lookup into Nodes

	// Lazy back-edge cache. Mutable so const queries can populate it.
	mutable bool bSuccessorCacheBuilt = false;
	mutable TMap<FNodeRef, TArray<FNodeRef>> SuccessorCache;

	void EnsureSuccessorCache() const;
	void Reset();
};
