#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AssemblyLineTypes.h"
#include "DAG/AssemblyLineDAG.h"  // FStationNode in OnDAGProposed payload (Story 32b)
#include "AgentChatSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAgentResponded, EStationType /*StationType*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRuleUpdated, EStationType /*StationType*/, const FString& /*NewRule*/);
// Story 32b — fired when the Orchestrator's chat reply contains a `dag` JSON
// spec that parses successfully. AAssemblyLineGameMode subscribes; the handler
// calls SpawnLineFromSpec to materialize the line. dag: null replies (small-talk)
// do NOT fire this delegate.
//
// Story 33b — payload extended with PromptsByKind: the Orchestrator-authored
// `## Role` text per spawned station kind, lifted from the reply's optional
// sibling `prompts` object. Empty map when the field is absent or malformed
// (non-fatal). The typedef sidesteps the comma-in-macro problem
// (`TMap<EStationType, FString>` would be parsed as two macro args).
using FAgentPromptsByKind = TMap<EStationType, FString>;
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDAGProposed,
	const TArray<FStationNode>& /*Nodes*/,
	const FAgentPromptsByKind& /*PromptsByKind*/);

UCLASS()
class ASSEMBLYLINESIMUL_API UAgentChatSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Append the user's message to the agent's history, build a contextual prompt with the
	// agent's role + current bucket state + prior chat, send to Claude, and on response speak
	// it via the agent's TalkPanel + macOS `say`.
	void SendMessage(EStationType StationType, const FString& UserText);

	// Read-only access to the conversation history for an agent. Returns an empty array if
	// the agent has never been addressed.
	const TArray<FAgentChatMessage>& GetHistory(EStationType StationType) const;

	// Builds the prompt that will be sent to Claude for the given agent + new user message.
	// Public for unit tests; in production the subsystem builds + sends in one step.
	FString BuildPromptForStation(EStationType StationType, const FString& UserText) const;

	UPROPERTY(EditAnywhere, Category = "AgentChat")
	bool bSpeakResponses = true;

	FOnAgentResponded OnAgentResponded;

	// Fires whenever a chat reply included a `new_rule` and the subsystem applied
	// it to the target station. Subscribers (UI banners, telemetry, tests) can
	// react without polling. Payload: which station + the new rule text.
	FOnRuleUpdated OnRuleUpdated;

	// Story 32b — fires when the Orchestrator's chat reply contains a `dag`
	// JSON spec that parses successfully. Payload: the parsed station nodes.
	FOnDAGProposed OnDAGProposed;

	// Public so external systems (e.g. the voice-hail handshake in the GameMode)
	// can push arbitrary text through the same macOS `say` pipeline used for chat
	// replies. No-op on non-Mac platforms. Records the last input into
	// LastSpokenForTesting so specs can assert the audible path was invoked.
	void SpeakResponse(const FString& Text);

	// Story 26 — push-to-talk silences agents. Terminates every in-flight
	// `/usr/bin/say` subprocess this subsystem spawned. AAssemblyLineGameMode
	// calls this from OnVoiceTalkStarted so the operator's first syllable
	// cuts off any agent that's mid-line. Mac-only; no-op elsewhere.
	UFUNCTION(BlueprintCallable, Category = "AgentChat")
	void StopSpeaking();

	// Inspection hook for tests — set every time SpeakResponse is called.
	mutable FString LastSpokenForTesting;

	// Story 26 test hook — exposes the live count of tracked say-subprocess
	// handles so specs can assert StopSpeaking emptied the store.
	int32 ActiveSayHandlesNumForTesting() const { return ActiveSayHandles.Num(); }

	// Public for tests — synthesise a Claude reply (the JSON body that would have
	// come back from the HTTP call) and feed it through the same code path the
	// production HTTP completion uses. Production code goes through SendMessage.
	void HandleClaudeResponse(EStationType StationType, bool bSuccess, const FString& Response);

private:
	TMap<EStationType, TArray<FAgentChatMessage>> Histories;
	FString GetRoleDescription(EStationType StationType) const;
	FString GetCurrentRule(EStationType StationType) const;
	FString GetCurrentBucketContents(EStationType StationType) const;
	FString StationTypeName(EStationType StationType) const;

	// Story 26 — every SpeakResponse parks its FProcHandle here so StopSpeaking
	// can SIGKILL the lot. Pruned of dead entries on each SpeakResponse call.
	TArray<FProcHandle> ActiveSayHandles;
};
