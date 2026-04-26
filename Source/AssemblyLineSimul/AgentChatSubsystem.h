#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AssemblyLineTypes.h"
#include "AgentChatSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAgentResponded, EStationType /*StationType*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRuleUpdated, EStationType /*StationType*/, const FString& /*NewRule*/);

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

	// Public so external systems (e.g. the voice-hail handshake in the GameMode)
	// can push arbitrary text through the same macOS `say` pipeline used for chat
	// replies. No-op on non-Mac platforms. Records the last input into
	// LastSpokenForTesting so specs can assert the audible path was invoked.
	void SpeakResponse(const FString& Text) const;

	// Inspection hook for tests — set every time SpeakResponse is called.
	mutable FString LastSpokenForTesting;

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
};
