#include "AgentChatSubsystem.h"
#include "AssemblyLineDirector.h"
#include "Bucket.h"
#include "ClaudeAPISubsystem.h"
#include "Station.h"
#include "WorkerRobot.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "JsonHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogAgentChat, Log, All);

using AssemblyLineJson::ExtractJsonObject;

void UAgentChatSubsystem::SendMessage(EStationType StationType, const FString& UserText)
{
	TArray<FAgentChatMessage>& History = Histories.FindOrAdd(StationType);
	History.Add({ TEXT("user"), UserText });

	const FString Prompt = BuildPromptForStation(StationType, UserText);

	UClaudeAPISubsystem* Claude = nullptr;
	if (UGameInstance* GI = GetGameInstance())
	{
		Claude = GI->GetSubsystem<UClaudeAPISubsystem>();
	}

	if (!Claude)
	{
		HandleClaudeResponse(StationType, false, TEXT("(Claude subsystem unavailable)"));
		return;
	}

	TWeakObjectPtr<UAgentChatSubsystem> WeakThis(this);
	const EStationType CapturedType = StationType;
	Claude->SendMessage(Prompt,
		FClaudeComplete::CreateLambda([WeakThis, CapturedType](bool bSuccess, const FString& Response)
		{
			if (UAgentChatSubsystem* Self = WeakThis.Get())
			{
				Self->HandleClaudeResponse(CapturedType, bSuccess, Response);
			}
		}));
}

const TArray<FAgentChatMessage>& UAgentChatSubsystem::GetHistory(EStationType StationType) const
{
	if (const TArray<FAgentChatMessage>* H = Histories.Find(StationType))
	{
		return *H;
	}
	static const TArray<FAgentChatMessage> Empty;
	return Empty;
}

FString UAgentChatSubsystem::BuildPromptForStation(EStationType StationType, const FString& UserText) const
{
	const FString Name = StationTypeName(StationType);
	const FString Role = GetRoleDescription(StationType);
	const FString Rule = GetCurrentRule(StationType);
	const FString Bucket = GetCurrentBucketContents(StationType);

	FString HistoryBlock;
	if (const TArray<FAgentChatMessage>* H = Histories.Find(StationType))
	{
		for (const FAgentChatMessage& M : *H)
		{
			HistoryBlock += FString::Printf(TEXT("%s: %s\n"),
				M.Role.Equals(TEXT("user"), ESearchCase::IgnoreCase) ? TEXT("User") : TEXT("You"),
				*M.Text);
		}
	}

	return FString::Printf(
		TEXT("You are the %s agent on an assembly line of AI workers. Your role: %s\n")
		TEXT("Current rule (governs how you process buckets): %s\n")
		TEXT("Current bucket contents: %s\n\n")
		TEXT("Conversation so far:\n%s")
		TEXT("User: %s\n\n")
		TEXT("The user may either: (a) chat with you, or (b) instruct you to change your rule. ")
		TEXT("If they tell you to change behavior (e.g. 'filter only odd numbers instead of primes'), ")
		TEXT("treat that as a NEW RULE and adopt it.\n\n")
		TEXT("Respond with ONLY a JSON object on a single line, no markdown:\n")
		TEXT("{\"reply\":\"<1-2 short conversational sentences>\",\"new_rule\":\"<rewritten plain-English rule>\"|null}\n")
		TEXT("Set 'new_rule' to the full rewritten rule when behavior changes; otherwise null. ")
		TEXT("'reply' must be plain English with no JSON or jargon."),
		*Name, *Role, *Rule, *Bucket, *HistoryBlock, *UserText);
}

void UAgentChatSubsystem::HandleClaudeResponse(EStationType StationType, bool bSuccess, const FString& Response)
{
	FString Reply;
	FString NewRule;

	if (bSuccess)
	{
		FString JsonStr;
		TSharedPtr<FJsonObject> Root;
		if (ExtractJsonObject(Response, JsonStr))
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
			FJsonSerializer::Deserialize(Reader, Root);
		}
		if (Root.IsValid())
		{
			Root->TryGetStringField(TEXT("reply"), Reply);
			Root->TryGetStringField(TEXT("new_rule"), NewRule);
		}
		if (Reply.IsEmpty())
		{
			// Fallback: treat the whole response as the reply if JSON parse failed.
			Reply = Response;
		}
	}
	else
	{
		Reply = FString::Printf(TEXT("(can't reach Claude: %s)"), *Response);
	}

	// Apply the new rule (if any) to the live station before broadcasting the reply.
	if (!NewRule.IsEmpty())
	{
		if (UWorld* W = GetWorld())
		{
			if (UAssemblyLineDirector* Director = W->GetSubsystem<UAssemblyLineDirector>())
			{
				if (AStation* Station = Director->GetStationOfType(StationType))
				{
					Station->CurrentRule = NewRule;
					Station->OnRuleSetByChat();
					UE_LOG(LogAgentChat, Log, TEXT("[%s] CurrentRule updated -> %s"),
						*StationTypeName(StationType), *NewRule);
				}
			}
		}
	}

	// Prepend "<AgentName> here. " so the spoken reply identifies who's talking and
	// confirms the command was understood — same radio-handshake feel as the hail
	// affirmation. Skip the prefix when Claude already led with the agent name.
	const FString AgentName = StationTypeName(StationType);
	const FString PrefixedReply = Reply.StartsWith(AgentName, ESearchCase::IgnoreCase)
		? Reply
		: FString::Printf(TEXT("%s here. %s"), *AgentName, *Reply);

	TArray<FAgentChatMessage>& History = Histories.FindOrAdd(StationType);
	History.Add({ TEXT("assistant"), PrefixedReply });

	UWorld* W = GetWorld();
	if (W)
	{
		if (UAssemblyLineDirector* Director = W->GetSubsystem<UAssemblyLineDirector>())
		{
			if (AWorkerRobot* Worker = Director->GetRobotForStation(StationType))
			{
				if (Worker->AssignedStation)
				{
					Worker->AssignedStation->SpeakStreaming(PrefixedReply);
				}
			}
		}
	}

	if (bSpeakResponses)
	{
		SpeakResponse(PrefixedReply);
	}

	OnAgentResponded.Broadcast(StationType);
}

void UAgentChatSubsystem::SpeakResponse(const FString& Text) const
{
#if PLATFORM_MAC
	if (Text.IsEmpty()) return;
	const FString TempPath = FPaths::ProjectSavedDir() / TEXT("agent_say_buffer.txt");
	if (!FFileHelper::SaveStringToFile(Text, *TempPath)) return;

	const FString Args = FString::Printf(TEXT("-f \"%s\""), *TempPath);
	FProcHandle H = FPlatformProcess::CreateProc(
		TEXT("/usr/bin/say"), *Args,
		/*bLaunchDetached=*/true, /*bLaunchHidden=*/true, /*bLaunchReallyHidden=*/true,
		nullptr, 0, nullptr, nullptr, nullptr);
	if (H.IsValid()) FPlatformProcess::CloseProc(H);
#endif
}

FString UAgentChatSubsystem::GetRoleDescription(EStationType StationType) const
{
	switch (StationType)
	{
	case EStationType::Generator:
		return TEXT("You generate a fresh bucket of integers at the start of each cycle, following whatever rule the user has given you.");
	case EStationType::Filter:
		return TEXT("You inspect each number in the input bucket and keep or remove items according to your rule.");
	case EStationType::Sorter:
		return TEXT("You receive a bucket from the Filter and reorder its items according to your rule (without adding or removing values).");
	case EStationType::Checker:
		return TEXT("You are the QA agent. You verify the bucket against your rule and either accept it or reject it; on reject you identify the prior station that likely caused the mistake.");
	default:
		return TEXT("Unknown role.");
	}
}

FString UAgentChatSubsystem::GetCurrentRule(EStationType StationType) const
{
	UWorld* W = GetWorld();
	if (!W) return TEXT("(unknown — no world)");
	UAssemblyLineDirector* Director = W->GetSubsystem<UAssemblyLineDirector>();
	if (!Director) return TEXT("(unknown — no director)");
	AStation* Station = Director->GetStationOfType(StationType);
	if (!Station) return TEXT("(unknown — station not registered)");
	return Station->GetEffectiveRule();
}

FString UAgentChatSubsystem::GetCurrentBucketContents(EStationType StationType) const
{
	UWorld* W = GetWorld();
	if (!W) return TEXT("(unknown — no world)");
	UAssemblyLineDirector* Director = W->GetSubsystem<UAssemblyLineDirector>();
	if (!Director) return TEXT("(unknown — no director)");
	AWorkerRobot* Worker = Director->GetRobotForStation(StationType);
	if (!Worker) return TEXT("(no worker assigned)");
	ABucket* Bucket = Worker->GetCurrentBucket();
	if (!Bucket) return TEXT("(no bucket in hand)");
	return Bucket->GetContentsString();
}

FString UAgentChatSubsystem::StationTypeName(EStationType StationType) const
{
	switch (StationType)
	{
	case EStationType::Generator: return TEXT("Generator");
	case EStationType::Filter:    return TEXT("Filter");
	case EStationType::Sorter:    return TEXT("Sorter");
	case EStationType::Checker:   return TEXT("Checker");
	default:                      return TEXT("Unknown");
	}
}
