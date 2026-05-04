#pragma once

#include "CoreMinimal.h"
#include "DAG/AssemblyLineDAG.h"

// Story 32a — parses the JSON spec the Orchestrator agent emits into
// a TArray<FStationNode> ready to feed FAssemblyLineDAG::BuildFromDAG.
//
// Expected JSON shape:
//   {
//     "nodes": [
//       {"id": "gen",  "type": "Generator", "rule": "..."},
//       {"id": "flt",  "type": "Filter",    "rule": "...", "parents": ["gen"]},
//       ...
//     ]
//   }
//
// IDs are arbitrary strings used only to express edges. Each node's
// JSON "type" maps to an EStationType; the (kind, instance) pair on the
// resulting FNodeRef is derived per-call (instance N is the N-th node of
// that kind in the spec, zero-indexed).
//
// Returns false on any of: malformed JSON, unknown station type,
// reference to an undeclared parent ID. Logs an Error under
// LogOrchestrator on failure.
namespace OrchestratorParser
{
	bool ParseDAGSpec(const FString& JsonText, TArray<FStationNode>& OutNodes);

	// Story 33b — accepts the FULL Claude reply object (with `dag` and
	// optional sibling `prompts`). Extracts both. The `prompts` map is
	// keyed by EStationType and contains the Orchestrator-authored Role
	// prose for each spawned kind. Missing `prompts` is non-fatal —
	// returns an empty map. Unknown station-type keys in `prompts` log
	// a Warning and are skipped.
	//
	// JSON shape:
	//   {"reply":"...","dag":{"nodes":[...]},
	//    "prompts":{"Filter":"<role prose>", ...}}
	bool ParsePlan(const FString& JsonText,
		TArray<FStationNode>& OutNodes,
		TMap<EStationType, FString>& OutPromptsByKind);
}
