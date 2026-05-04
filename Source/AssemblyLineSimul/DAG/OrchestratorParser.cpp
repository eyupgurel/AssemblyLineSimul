#include "DAG/OrchestratorParser.h"
#include "AssemblyLineTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogOrchestrator, Log, All);

namespace
{
	bool TryParseStationType(const FString& Name, EStationType& Out)
	{
		if (Name.Equals(TEXT("Generator"), ESearchCase::IgnoreCase))    { Out = EStationType::Generator;    return true; }
		if (Name.Equals(TEXT("Filter"),    ESearchCase::IgnoreCase))    { Out = EStationType::Filter;       return true; }
		if (Name.Equals(TEXT("Sorter"),    ESearchCase::IgnoreCase))    { Out = EStationType::Sorter;       return true; }
		if (Name.Equals(TEXT("Checker"),   ESearchCase::IgnoreCase))    { Out = EStationType::Checker;      return true; }
		if (Name.Equals(TEXT("Orchestrator"), ESearchCase::IgnoreCase)) { Out = EStationType::Orchestrator; return true; }
		return false;
	}
}

namespace
{
	// Shared parse path. Caller passes a Root JSON object that already has
	// the `nodes` array at the top level (the original ParseDAGSpec shape).
	// ParsePlan finds the nodes array nested under `dag` and calls in.
	bool PopulateNodesFromArray(const TArray<TSharedPtr<FJsonValue>>& Nodes,
		TArray<FStationNode>& OutNodes)
	{
		// First pass — assign FNodeRef per JSON ID. Instance is the
		// zero-indexed position of this kind in the spec.
		TMap<FString, FNodeRef> IdToRef;
		TMap<EStationType, int32> InstanceCounters;
		IdToRef.Reserve(Nodes.Num());
		OutNodes.Reserve(Nodes.Num());

		for (const TSharedPtr<FJsonValue>& Item : Nodes)
		{
			const TSharedPtr<FJsonObject>* NodeObj = nullptr;
			if (!Item.IsValid() || !Item->TryGetObject(NodeObj) || !NodeObj || !NodeObj->IsValid())
			{
				UE_LOG(LogOrchestrator, Error,
					TEXT("ParseDAGSpec: node entry is not an object"));
				return false;
			}

			FString Id, TypeStr, Rule;
			(*NodeObj)->TryGetStringField(TEXT("id"),   Id);
			(*NodeObj)->TryGetStringField(TEXT("type"), TypeStr);
			(*NodeObj)->TryGetStringField(TEXT("rule"), Rule);
			if (Id.IsEmpty() || TypeStr.IsEmpty())
			{
				UE_LOG(LogOrchestrator, Error,
					TEXT("ParseDAGSpec: node missing 'id' or 'type'"));
				return false;
			}

			EStationType Kind;
			if (!TryParseStationType(TypeStr, Kind))
			{
				UE_LOG(LogOrchestrator, Error,
					TEXT("ParseDAGSpec: unknown station type '%s'"), *TypeStr);
				return false;
			}

			const int32 Instance = InstanceCounters.FindOrAdd(Kind, 0);
			InstanceCounters[Kind] = Instance + 1;

			FStationNode N;
			N.Ref  = FNodeRef{Kind, Instance};
			N.Rule = Rule;
			IdToRef.Add(Id, N.Ref);
			OutNodes.Add(MoveTemp(N));
		}

		// Second pass — resolve parent IDs against the IdToRef map.
		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* NodeObj = nullptr;
			Nodes[i]->TryGetObject(NodeObj);

			const TArray<TSharedPtr<FJsonValue>>* Parents = nullptr;
			if (!(*NodeObj)->TryGetArrayField(TEXT("parents"), Parents) || !Parents)
			{
				continue;  // no parents — source node, fine
			}

			for (const TSharedPtr<FJsonValue>& P : *Parents)
			{
				FString ParentId;
				if (!P.IsValid() || !P->TryGetString(ParentId))
				{
					UE_LOG(LogOrchestrator, Error,
						TEXT("ParseDAGSpec: parent entry not a string"));
					OutNodes.Reset();
					return false;
				}
				const FNodeRef* ParentRef = IdToRef.Find(ParentId);
				if (!ParentRef)
				{
					UE_LOG(LogOrchestrator, Error,
						TEXT("ParseDAGSpec: parent id '%s' not declared in nodes"),
						*ParentId);
					OutNodes.Reset();
					return false;
				}
				OutNodes[i].Parents.Add(*ParentRef);
			}
		}

		return true;
	}
}

namespace OrchestratorParser
{
	bool ParseDAGSpec(const FString& JsonText, TArray<FStationNode>& OutNodes)
	{
		OutNodes.Reset();

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(LogOrchestrator, Error,
				TEXT("ParseDAGSpec: malformed JSON: %s"), *JsonText);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!Root->TryGetArrayField(TEXT("nodes"), Nodes) || !Nodes)
		{
			UE_LOG(LogOrchestrator, Error,
				TEXT("ParseDAGSpec: top-level 'nodes' array missing"));
			return false;
		}

		return PopulateNodesFromArray(*Nodes, OutNodes);
	}

	bool ParsePlan(const FString& JsonText,
		TArray<FStationNode>& OutNodes,
		TMap<EStationType, FString>& OutPromptsByKind)
	{
		OutNodes.Reset();
		OutPromptsByKind.Reset();

		// Parse the full reply object: {"reply":..., "dag":{"nodes":[...]},
		// "prompts":{...}}.
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(LogOrchestrator, Error,
				TEXT("ParsePlan: malformed JSON: %s"), *JsonText);
			return false;
		}

		// `dag` is required.
		const TSharedPtr<FJsonObject>* DagObj = nullptr;
		if (!Root->TryGetObjectField(TEXT("dag"), DagObj) || !DagObj || !DagObj->IsValid())
		{
			UE_LOG(LogOrchestrator, Error,
				TEXT("ParsePlan: 'dag' object missing or not an object"));
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* DagNodes = nullptr;
		if (!(*DagObj)->TryGetArrayField(TEXT("nodes"), DagNodes) || !DagNodes)
		{
			UE_LOG(LogOrchestrator, Error,
				TEXT("ParsePlan: 'dag.nodes' array missing"));
			return false;
		}

		if (!PopulateNodesFromArray(*DagNodes, OutNodes))
		{
			return false;  // PopulateNodesFromArray already logged
		}

		// `prompts` is optional.
		const TSharedPtr<FJsonObject>* PromptsObj = nullptr;
		if (Root->TryGetObjectField(TEXT("prompts"), PromptsObj) && PromptsObj && PromptsObj->IsValid())
		{
			for (const auto& Pair : (*PromptsObj)->Values)
			{
				EStationType Kind;
				if (!TryParseStationType(Pair.Key, Kind))
				{
					UE_LOG(LogOrchestrator, Error,
						TEXT("ParsePlan: 'prompts' entry has unknown station type '%s' — skipped"),
						*Pair.Key);
					continue;
				}
				FString Prose;
				if (Pair.Value.IsValid() && Pair.Value->TryGetString(Prose))
				{
					OutPromptsByKind.Add(Kind, MoveTemp(Prose));
				}
			}
		}

		return true;
	}
}
