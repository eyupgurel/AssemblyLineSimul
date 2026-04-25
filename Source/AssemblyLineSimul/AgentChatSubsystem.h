#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AssemblyLineTypes.h"
#include "AgentChatSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAgentResponded, EStationType /*StationType*/);

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

private:
	TMap<EStationType, TArray<FAgentChatMessage>> Histories;

	void HandleClaudeResponse(EStationType StationType, bool bSuccess, const FString& Response);
	void SpeakResponse(const FString& Text) const;
	FString GetRoleDescription(EStationType StationType) const;
	FString GetCurrentBucketContents(EStationType StationType) const;
	FString StationTypeName(EStationType StationType) const;
};
