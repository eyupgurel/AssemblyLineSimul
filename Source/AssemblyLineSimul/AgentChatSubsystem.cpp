#include "AgentChatSubsystem.h"
#include "AssemblyLineDirector.h"
#include "Bucket.h"
#include "ClaudeAPISubsystem.h"
#include "Station.h"
#include "WorkerRobot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogAgentChat, Log, All);

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
		TEXT("Current bucket contents: %s\n\n")
		TEXT("Conversation so far:\n%s")
		TEXT("User: %s\n\n")
		TEXT("Respond in 1-2 short sentences, conversationally, in plain English. ")
		TEXT("Do not include any prefix like 'Filter:' or 'Agent:' — just your reply text."),
		*Name, *Role, *Bucket, *HistoryBlock, *UserText);
}

void UAgentChatSubsystem::HandleClaudeResponse(EStationType StationType, bool bSuccess, const FString& Response)
{
	const FString Reply = bSuccess
		? Response
		: FString::Printf(TEXT("(can't reach Claude: %s)"), *Response);

	TArray<FAgentChatMessage>& History = Histories.FindOrAdd(StationType);
	History.Add({ TEXT("assistant"), Reply });

	UWorld* W = GetWorld();
	if (W)
	{
		if (UAssemblyLineDirector* Director = W->GetSubsystem<UAssemblyLineDirector>())
		{
			if (AWorkerRobot* Worker = Director->GetRobotForStation(StationType))
			{
				if (Worker->AssignedStation)
				{
					Worker->AssignedStation->SpeakStreaming(Reply);
				}
			}
		}
	}

	if (bSpeakResponses)
	{
		SpeakResponse(Reply);
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
		return TEXT("You generate a fresh bucket of 10 random integers in the range 1-100 at the start of each cycle.");
	case EStationType::Filter:
		return TEXT("You inspect each number in the bucket and remove the non-primes, keeping only the primes. You sometimes make mistakes (~15% error rate) and let a non-prime through.");
	case EStationType::Sorter:
		return TEXT("You receive a bucket of primes from the Filter and sort them in strictly ascending order. You sometimes make mistakes (~15% error rate) and swap two adjacent values.");
	case EStationType::Checker:
		return TEXT("You are the QA agent. You verify the bucket contains only primes in [1, 100] sorted ascending. If not, you reject the bucket and identify which station made the mistake (Filter for non-primes, Sorter for ordering).");
	default:
		return TEXT("Unknown role.");
	}
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
