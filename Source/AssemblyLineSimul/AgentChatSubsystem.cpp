#include "AgentChatSubsystem.h"
#include "AgentPromptLibrary.h"
#include "AssemblyLineDirector.h"
#include "Bucket.h"
#include "ClaudeAPISubsystem.h"
#include "DAG/OrchestratorParser.h"
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

	// Story 32a — Orchestrator uses a different prompt template that asks
	// for {"reply", "dag"} instead of {"reply", "new_rule"}. Per-purpose
	// template; chat-side parsing in 32b will switch on the same enum.
	const FString TemplateSection = (StationType == EStationType::Orchestrator)
		? TEXT("OrchestratorChatPromptTemplate")
		: TEXT("ChatPromptTemplate");

	return AgentPromptLibrary::FormatPrompt(
		AgentPromptLibrary::LoadChatSection(TemplateSection),
		{
			{TEXT("agent"),   Name},
			{TEXT("role"),    Role},
			{TEXT("rule"),    Rule},
			{TEXT("bucket"),  Bucket},
			{TEXT("history"), HistoryBlock},
			{TEXT("message"), UserText},
		});
}

void UAgentChatSubsystem::HandleClaudeResponse(EStationType StationType, bool bSuccess, const FString& Response)
{
	FString Reply;
	FString NewRule;
	TSharedPtr<FJsonObject> Root;
	// Hoisted so the Orchestrator branch below can pass the cleaned inner
	// JSON (post-ExtractJsonObject) to ParsePlan. Passing the raw Response
	// would fail when Claude wraps the JSON in prose or ```json fences.
	FString JsonStr;

	if (bSuccess)
	{
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

	// Story 32b — Orchestrator-only: if the reply contained a non-null `dag`
	// object, run it through OrchestratorParser. On a successful parse,
	// broadcast OnDAGProposed so AAssemblyLineGameMode can spawn the line.
	// dag: null (small-talk) or non-Orchestrator agents skip this entirely.
	//
	// Story 33b — also extracts the optional sibling `prompts` object via
	// ParsePlan so the spawn handler can write Orchestrator-authored
	// per-agent .md files before the line materializes.
	if (bSuccess && StationType == EStationType::Orchestrator && Root.IsValid())
	{
		const TSharedPtr<FJsonValue> DagValue = Root->TryGetField(TEXT("dag"));
		if (DagValue.IsValid() && DagValue->Type == EJson::Object)
		{
			TArray<FStationNode> Nodes;
			TMap<EStationType, FString> PromptsByKind;
			// Pass JsonStr (the post-extraction inner JSON), not Response —
			// Claude often wraps the actual JSON in prose / fences and
			// ParsePlan would fail on that.
			if (OrchestratorParser::ParsePlan(JsonStr, Nodes, PromptsByKind))
			{
				UE_LOG(LogAgentChat, Display,
					TEXT("[Orchestrator] Plan accepted (%d nodes, %d prompts) — broadcasting OnDAGProposed"),
					Nodes.Num(), PromptsByKind.Num());
				OnDAGProposed.Broadcast(Nodes, PromptsByKind);
			}
			else
			{
				// Most common cause: Claude's reply was truncated at MaxTokens
				// mid-JSON, leaving an unparseable object. Surface so the
				// operator doesn't wonder why the line never spawned.
				UE_LOG(LogAgentChat, Warning,
					TEXT("[Orchestrator] dag-shaped reply received but ParsePlan failed — "
					     "likely truncated (raise UClaudeAPISubsystem::MaxTokens) or malformed. "
					     "Inner JSON: %s"), *JsonStr);
			}
		}
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
				}
			}
		}
		// Display-level so it's obvious in the editor console — Story 17 AC17.1.
		UE_LOG(LogAgentChat, Display, TEXT("[%s] CurrentRule updated → \"%s\""),
			*StationTypeName(StationType), *NewRule);

		// Broadcast for subscribers (UI banners, telemetry, tests).
		OnRuleUpdated.Broadcast(StationType, NewRule);

		// Audible + visible confirmation that the rule actually took effect — AC17.2.
		// Spoken in addition to Claude's conversational `reply` so the audience hears
		// the change even if they missed the chat reply.
		const FString RuleConfirm = FString::Printf(
			TEXT("Rule updated. From now on I will %s"), *NewRule);
		SpeakResponse(RuleConfirm);
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

	if (bSpeakResponses)
	{
		SpeakResponse(PrefixedReply);
	}

	OnAgentResponded.Broadcast(StationType);
}

void UAgentChatSubsystem::SpeakResponse(const FString& Text)
{
	// Record for tests on every call (cheap; doesn't depend on platform).
	LastSpokenForTesting = Text;
#if PLATFORM_MAC
	if (Text.IsEmpty()) return;

	// Story 26 — drop already-finished say processes from the store so it
	// stays bounded. Non-running handles still need CloseProc to release.
	for (int32 i = ActiveSayHandles.Num() - 1; i >= 0; --i)
	{
		if (!FPlatformProcess::IsProcRunning(ActiveSayHandles[i]))
		{
			FPlatformProcess::CloseProc(ActiveSayHandles[i]);
			ActiveSayHandles.RemoveAt(i);
		}
	}

	const FString TempPath = FPaths::ProjectSavedDir() / TEXT("agent_say_buffer.txt");
	if (!FFileHelper::SaveStringToFile(Text, *TempPath)) return;

	const FString Args = FString::Printf(TEXT("-f \"%s\""), *TempPath);
	FProcHandle H = FPlatformProcess::CreateProc(
		TEXT("/usr/bin/say"), *Args,
		/*bLaunchDetached=*/true, /*bLaunchHidden=*/true, /*bLaunchReallyHidden=*/true,
		nullptr, 0, nullptr, nullptr, nullptr);
	// Story 26 — store the handle (don't CloseProc immediately like the old
	// code did) so StopSpeaking can SIGKILL the say process when the
	// operator pushes Space. CloseProc happens in StopSpeaking or in the
	// dead-handle prune above.
	if (H.IsValid()) ActiveSayHandles.Add(H);
#endif
}

void UAgentChatSubsystem::StopSpeaking()
{
#if PLATFORM_MAC
	for (FProcHandle& H : ActiveSayHandles)
	{
		if (FPlatformProcess::IsProcRunning(H))
		{
			FPlatformProcess::TerminateProc(H);
		}
		FPlatformProcess::CloseProc(H);
	}
	ActiveSayHandles.Reset();
#endif
}

FString UAgentChatSubsystem::GetRoleDescription(EStationType StationType) const
{
	return AgentPromptLibrary::LoadAgentSection(StationType, TEXT("Role"));
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
	case EStationType::Generator:    return TEXT("Generator");
	case EStationType::Filter:       return TEXT("Filter");
	case EStationType::Sorter:       return TEXT("Sorter");
	case EStationType::Checker:      return TEXT("Checker");
	case EStationType::Orchestrator: return TEXT("Orchestrator");
	default:                         return TEXT("Unknown");
	}
}
