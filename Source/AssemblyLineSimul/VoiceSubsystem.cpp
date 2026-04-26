#include "VoiceSubsystem.h"
#include "AgentChatSubsystem.h"
#include "Engine/GameInstance.h"
#include "VoiceHailParser.h"

DEFINE_LOG_CATEGORY_STATIC(LogVoice, Log, All);

namespace
{
	const TCHAR* StationName(EStationType T)
	{
		switch (T)
		{
		case EStationType::Generator: return TEXT("Generator");
		case EStationType::Filter:    return TEXT("Filter");
		case EStationType::Sorter:    return TEXT("Sorter");
		case EStationType::Checker:   return TEXT("Checker");
		default:                      return TEXT("Unknown");
		}
	}
}

void UVoiceSubsystem::HandleTranscript(const FString& Transcript)
{
	if (Transcript.TrimStartAndEnd().IsEmpty())
	{
		UE_LOG(LogVoice, Verbose, TEXT("Empty transcript ignored."));
		return;
	}

	EStationType Hailed;
	if (AssemblyLineVoice::TryParseHail(Transcript, Hailed))
	{
		UE_LOG(LogVoice, Log, TEXT("Hail detected → active agent now %s"), StationName(Hailed));
		SetActiveAgent(Hailed);
		return;
	}

	if (!bHasActive)
	{
		UE_LOG(LogVoice, Warning,
			TEXT("Transcript with no active agent dropped: '%s' (hail an agent first, e.g. 'hey filter do you read me')"),
			*Transcript);
		return;
	}

	// Route as a command to the active agent.
	if (UAgentChatSubsystem* Chat = ResolveChat())
	{
		UE_LOG(LogVoice, Log, TEXT("Routing transcript to %s: '%s'"),
			StationName(ActiveAgent), *Transcript);
		Chat->SendMessage(ActiveAgent, Transcript);
	}
}

UAgentChatSubsystem* UVoiceSubsystem::ResolveChat() const
{
	if (ChatOverride) return ChatOverride;
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UAgentChatSubsystem>();
	}
	return nullptr;
}

void UVoiceSubsystem::SetActiveAgent(EStationType Agent)
{
	const bool bChanged = !bHasActive || ActiveAgent != Agent;
	bHasActive = true;
	ActiveAgent = Agent;
	if (bChanged)
	{
		OnActiveAgentChanged.Broadcast(Agent);
	}
}

FString UVoiceSubsystem::AffirmationFor(EStationType Agent) const
{
	return FString::Printf(TEXT("%s here, ready for your command."), StationName(Agent));
}
