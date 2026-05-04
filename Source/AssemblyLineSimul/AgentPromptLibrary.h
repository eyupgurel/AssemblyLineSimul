#pragma once

#include "CoreMinimal.h"
#include "AssemblyLineTypes.h"

// Story 27 — every Claude-bound prompt template lives in
// Content/Agents/<Agent>.md (and Content/Agents/ChatPrompt.md for the
// shared chat template). This namespace loads those sections into
// memory on first use, caches them for the process lifetime, and
// substitutes {{name}} placeholders.
//
// File layout:
//   Content/Agents/Generator.md
//   Content/Agents/Filter.md
//   Content/Agents/Sorter.md
//   Content/Agents/Checker.md   — adds DerivedRuleTemplate section
//   Content/Agents/ChatPrompt.md
//
// Each agent .md contains sections introduced by `## SectionName`
// followed by a body. Sections we read today: DefaultRule, Role,
// ProcessBucketPrompt (and DerivedRuleTemplate on Checker).
namespace AgentPromptLibrary
{
	// Returns the verbatim body of the named section in <Agent>.md, with
	// leading and trailing whitespace trimmed. Returns empty string and
	// logs a warning if the section is missing.
	FString LoadAgentSection(EStationType Agent, const FString& SectionName);

	// Same as LoadAgentSection but reads from the shared ChatPrompt.md.
	FString LoadChatSection(const FString& SectionName);

	// Substitutes {{name}} in the template with each Vars entry.
	// Unresolved {{...}} placeholders are left intact in the output and
	// the first one logs a warning so misses are obvious.
	FString FormatPrompt(FString Template, const TMap<FString, FString>& Vars);

	// Story 33b — clears both the per-agent and chat caches so the next
	// LoadAgentSection / LoadChatSection re-reads from disk. Used after
	// the spawn handler writes Orchestrator-authored prompts into
	// Saved/Agents/, so freshly-spawned stations pick them up rather
	// than serving the static Content/Agents/ content from cache.
	void InvalidateCache();
}
