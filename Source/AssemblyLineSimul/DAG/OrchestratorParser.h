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
}
