#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AssemblyLineTypes.h"
#include "VoiceSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnActiveAgentChanged, EStationType /*Agent*/);

UCLASS()
class ASSEMBLYLINESIMUL_API UVoiceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Process a transcript (e.g. from Whisper). If it parses as a hail, switch active
	// agent and speak an affirmation. Otherwise, route as a command to the active
	// agent via UAgentChatSubsystem. With no active agent yet, non-hail transcripts
	// are dropped (logged).
	void HandleTranscript(const FString& Transcript);

	bool HasActiveAgent() const { return bHasActive; }
	EStationType GetActiveAgent() const { return ActiveAgent; }

	// Tests construct subsystems via NewObject and can't go through the GI's
	// subsystem collection, so they inject the chat subsystem directly. In
	// production this stays null and HandleTranscript falls back to the GI lookup.
	void SetChatSubsystemForTesting(class UAgentChatSubsystem* Chat) { ChatOverride = Chat; }

	FOnActiveAgentChanged OnActiveAgentChanged;

private:
	// Story 32b — default-active is the Orchestrator so the operator's first
	// push-to-talk routes to it without needing a hail (mission-driven boot).
	bool bHasActive = true;
	EStationType ActiveAgent = EStationType::Orchestrator;

	UPROPERTY()
	TObjectPtr<UAgentChatSubsystem> ChatOverride;

	void SetActiveAgent(EStationType Agent);
	FString AffirmationFor(EStationType Agent) const;
	UAgentChatSubsystem* ResolveChat() const;
};
