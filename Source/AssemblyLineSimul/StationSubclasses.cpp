#include "StationSubclasses.h"
#include "AgentPromptLibrary.h"
#include "AssemblyLineDirector.h"
#include "Bucket.h"
#include "ClaudeAPISubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "JsonHelpers.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogStation, Log, All);

using AssemblyLineJson::ExtractJsonObject;

namespace
{
	bool IsPrime(int32 N)
	{
		if (N < 2) return false;
		if (N < 4) return true;
		if (N % 2 == 0) return false;
		for (int32 i = 3; i * i <= N; i += 2)
		{
			if (N % i == 0) return false;
		}
		return true;
	}

	bool ParseResultArray(const FString& Response, TArray<int32>& OutNumbers)
	{
		FString JsonStr;
		if (!ExtractJsonObject(Response, JsonStr)) return false;

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;

		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Root->TryGetArrayField(TEXT("result"), Arr) || !Arr) return false;

		OutNumbers.Reset(Arr->Num());
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			if (!V.IsValid()) continue;
			double D = 0.0;
			if (V->TryGetNumber(D))
			{
				OutNumbers.Add(FMath::RoundToInt(D));
			}
		}
		return true;
	}

	UClaudeAPISubsystem* GetClaude(const AActor* Actor)
	{
		if (!Actor) return nullptr;
		UWorld* W = Actor->GetWorld();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UClaudeAPISubsystem>() : nullptr;
	}
}

// ---- Generator ----------------------------------------------------------------

AGeneratorStation::AGeneratorStation()
{
	StationType = EStationType::Generator;
	DisplayName = TEXT("GENERATOR");
	CurrentRule = AgentPromptLibrary::LoadAgentSection(
		EStationType::Generator, TEXT("DefaultRule"));
}

void AGeneratorStation::ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete)
{
	if (!Bucket)
	{
		FStationProcessResult R; R.bAccepted = false; R.Reason = TEXT("Null bucket");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	UClaudeAPISubsystem* Claude = GetClaude(this);
	if (!Claude)
	{
		FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM unreachable");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	const FString EffectiveRule = GetEffectiveRule();
	UE_LOG(LogStation, Display,
		TEXT("[Generator] ProcessBucket using rule: \"%s\""), *EffectiveRule);
	const FString Prompt = AgentPromptLibrary::FormatPrompt(
		AgentPromptLibrary::LoadAgentSection(
			EStationType::Generator, TEXT("ProcessBucketPrompt")),
		{ {TEXT("rule"), EffectiveRule} });

	TWeakObjectPtr<AGeneratorStation> WeakThis(this);
	TWeakObjectPtr<ABucket> WeakBucket(Bucket);
	Claude->SendMessage(Prompt,
		FClaudeComplete::CreateLambda([WeakThis, WeakBucket, OnComplete](bool bSuccess, const FString& Response)
		{
			AGeneratorStation* Self = WeakThis.Get();
			ABucket* B = WeakBucket.Get();
			if (!Self || !B)
			{
				FStationProcessResult R; R.bAccepted = true;
				OnComplete.ExecuteIfBound(R);
				return;
			}

			TArray<int32> Numbers;
			if (!bSuccess || !ParseResultArray(Response, Numbers))
			{
				FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM failed");
				OnComplete.ExecuteIfBound(R);
				return;
			}

			B->Contents = MoveTemp(Numbers);
			B->RefreshContents();

			// Hold so the cinematic (which just zoomed in on OnContentsRevealed) has time
			// to show the freshly-filled bucket, matching the Working-state wait other
			// stations get from WorkDuration.
			UWorld* W = Self->GetWorld();
			if (!W)
			{
				FStationProcessResult R; R.bAccepted = true;
				OnComplete.ExecuteIfBound(R);
				return;
			}
			FTimerHandle Th;
			W->GetTimerManager().SetTimer(Th,
				FTimerDelegate::CreateLambda([OnComplete]()
				{
					FStationProcessResult R; R.bAccepted = true;
					OnComplete.ExecuteIfBound(R);
				}),
				5.0f, false);
		}));
}

// ---- Filter -------------------------------------------------------------------

AFilterStation::AFilterStation()
{
	StationType = EStationType::Filter;
	DisplayName = TEXT("FILTER");
	CurrentRule = AgentPromptLibrary::LoadAgentSection(
		EStationType::Filter, TEXT("DefaultRule"));
}

TArray<int32> AFilterStation::FindKeptIndices(
	const TArray<int32>& InputContents, const TArray<int32>& KeptValues)
{
	TArray<int32> Result;
	TSet<int32> Claimed;
	for (int32 KeptValue : KeptValues)
	{
		for (int32 i = 0; i < InputContents.Num(); ++i)
		{
			if (InputContents[i] == KeptValue && !Claimed.Contains(i))
			{
				Result.Add(i);
				Claimed.Add(i);
				break;
			}
		}
	}
	return Result;
}

void AFilterStation::ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete)
{
	if (!Bucket)
	{
		FStationProcessResult R; R.bAccepted = false; R.Reason = TEXT("Null bucket");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	UClaudeAPISubsystem* Claude = GetClaude(this);
	if (!Claude)
	{
		FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM unreachable");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	const FString Input = Bucket->GetContentsString();
	const FString EffectiveRule = GetEffectiveRule();
	UE_LOG(LogStation, Display,
		TEXT("[Filter] ProcessBucket using rule: \"%s\" — input: %s"),
		*EffectiveRule, *Input);
	const FString Prompt = AgentPromptLibrary::FormatPrompt(
		AgentPromptLibrary::LoadAgentSection(
			EStationType::Filter, TEXT("ProcessBucketPrompt")),
		{ {TEXT("rule"), EffectiveRule}, {TEXT("input"), Input} });

	TWeakObjectPtr<AFilterStation> WeakThis(this);
	TWeakObjectPtr<ABucket> WeakBucket(Bucket);
	Claude->SendMessage(Prompt,
		FClaudeComplete::CreateLambda([WeakThis, WeakBucket, OnComplete](bool bSuccess, const FString& Response)
		{
			AFilterStation* Self = WeakThis.Get();
			ABucket* B = WeakBucket.Get();
			if (!Self || !B)
			{
				FStationProcessResult R; R.bAccepted = true;
				OnComplete.ExecuteIfBound(R);
				return;
			}

			TArray<int32> Numbers;
			if (!bSuccess || !ParseResultArray(Response, Numbers))
			{
				FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM failed");
				OnComplete.ExecuteIfBound(R);
				return;
			}

			// Story 25 — selection preview: keep all input balls visible,
			// glow only the kept ones for 1 s, then drop the rejected.
			const TArray<int32> KeptIndices = AFilterStation::FindKeptIndices(B->Contents, Numbers);
			B->HighlightBallsAtIndices(KeptIndices);

			UWorld* W = Self->GetWorld();
			if (!W)
			{
				B->Contents = MoveTemp(Numbers);
				B->RefreshContents();
				FStationProcessResult R; R.bAccepted = true;
				OnComplete.ExecuteIfBound(R);
				return;
			}

			TWeakObjectPtr<ABucket> WeakBucket2(B);
			TArray<int32> KeptValues = MoveTemp(Numbers);
			FTimerHandle Th;
			W->GetTimerManager().SetTimer(Th,
				FTimerDelegate::CreateLambda([WeakBucket2, KeptValues, OnComplete]()
				{
					if (ABucket* B2 = WeakBucket2.Get())
					{
						B2->Contents = KeptValues;
						B2->RefreshContents();
					}
					FStationProcessResult R; R.bAccepted = true;
					OnComplete.ExecuteIfBound(R);
				}),
				1.0f, /*bLoop=*/false);
		}));
}

// ---- Sorter -------------------------------------------------------------------

ASorterStation::ASorterStation()
{
	StationType = EStationType::Sorter;
	DisplayName = TEXT("SORTER");
	CurrentRule = AgentPromptLibrary::LoadAgentSection(
		EStationType::Sorter, TEXT("DefaultRule"));
}

void ASorterStation::ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete)
{
	if (!Bucket)
	{
		FStationProcessResult R; R.bAccepted = false; R.Reason = TEXT("Null bucket");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	UClaudeAPISubsystem* Claude = GetClaude(this);
	if (!Claude)
	{
		FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM unreachable");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	const FString Input = Bucket->GetContentsString();
	const FString EffectiveRule = GetEffectiveRule();
	UE_LOG(LogStation, Display,
		TEXT("[Sorter] ProcessBucket using rule: \"%s\" — input: %s"),
		*EffectiveRule, *Input);
	const FString Prompt = AgentPromptLibrary::FormatPrompt(
		AgentPromptLibrary::LoadAgentSection(
			EStationType::Sorter, TEXT("ProcessBucketPrompt")),
		{ {TEXT("rule"), EffectiveRule}, {TEXT("input"), Input} });

	TWeakObjectPtr<ASorterStation> WeakThis(this);
	TWeakObjectPtr<ABucket> WeakBucket(Bucket);
	Claude->SendMessage(Prompt,
		FClaudeComplete::CreateLambda([WeakThis, WeakBucket, OnComplete](bool bSuccess, const FString& Response)
		{
			ASorterStation* Self = WeakThis.Get();
			ABucket* B = WeakBucket.Get();
			if (!Self || !B)
			{
				FStationProcessResult R; R.bAccepted = true;
				OnComplete.ExecuteIfBound(R);
				return;
			}

			TArray<int32> Numbers;
			if (!bSuccess || !ParseResultArray(Response, Numbers))
			{
				FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM failed");
				OnComplete.ExecuteIfBound(R);
				return;
			}

			B->Contents = MoveTemp(Numbers);
			B->RefreshContents();

			FStationProcessResult R; R.bAccepted = true;
			OnComplete.ExecuteIfBound(R);
		}));
}

// ---- Checker (LLM) ------------------------------------------------------------

ACheckerStation::ACheckerStation()
{
	StationType = EStationType::Checker;
	DisplayName = TEXT("CHECKER (LLM)");
	// Default CurrentRule is only used if the user later switches Checker out of derived
	// mode by giving it an explicit rule via chat. While bUseDerivedRule is true,
	// GetEffectiveRule composes Generator + Filter + Sorter rules at read time.
	CurrentRule = AgentPromptLibrary::LoadAgentSection(
		EStationType::Checker, TEXT("DefaultRule"));
}

FString ACheckerStation::GetEffectiveRule() const
{
	if (!bUseDerivedRule)
	{
		return CurrentRule;
	}

	UWorld* W = GetWorld();
	UAssemblyLineDirector* Director = W ? W->GetSubsystem<UAssemblyLineDirector>() : nullptr;
	if (!Director)
	{
		return CurrentRule;
	}

	auto RuleOf = [Director](EStationType T)
	{
		AStation* S = Director->GetStationOfType(T);
		return S ? S->CurrentRule : FString(TEXT("(unknown)"));
	};

	return AgentPromptLibrary::FormatPrompt(
		AgentPromptLibrary::LoadAgentSection(
			EStationType::Checker, TEXT("DerivedRuleTemplate")),
		{
			{TEXT("generator_rule"), RuleOf(EStationType::Generator)},
			{TEXT("filter_rule"),    RuleOf(EStationType::Filter)},
			{TEXT("sorter_rule"),    RuleOf(EStationType::Sorter)},
		});
}

void ACheckerStation::OnRuleSetByChat()
{
	if (bUseDerivedRule)
	{
		bUseDerivedRule = false;
		UE_LOG(LogStation, Log,
			TEXT("Checker switched to explicit chat-driven rule (derived mode off)."));
	}
}

void ACheckerStation::ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete)
{
	if (!Bucket)
	{
		FStationProcessResult R; R.bAccepted = false; R.Reason = TEXT("Null bucket");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	UClaudeAPISubsystem* Claude = GetClaude(this);
	const FString Numbers = Bucket->GetContentsString();
	const FString EffectiveRule = GetEffectiveRule();
	UE_LOG(LogStation, Display,
		TEXT("[Checker] ProcessBucket using rule: \"%s\" — input: %s"),
		*EffectiveRule, *Numbers);
	const FString Prompt = AgentPromptLibrary::FormatPrompt(
		AgentPromptLibrary::LoadAgentSection(
			EStationType::Checker, TEXT("ProcessBucketPrompt")),
		{ {TEXT("rule"), EffectiveRule}, {TEXT("bucket"), Numbers} });

	if (!Claude)
	{
		FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM unreachable, accepting by default");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	TWeakObjectPtr<ACheckerStation> WeakThis(this);
	Claude->SendMessage(Prompt,
		FClaudeComplete::CreateLambda([WeakThis, OnComplete](bool bSuccess, const FString& Response)
		{
			if (ACheckerStation* Self = WeakThis.Get())
			{
				Self->HandleVerdictReply(bSuccess, Response, OnComplete);
			}
		}));
}

void ACheckerStation::HandleVerdictReply(bool bSuccess, const FString& Response, FStationProcessComplete OnComplete)
{
	FStationProcessResult R;
	if (!bSuccess)
	{
		R.bAccepted = true;
		R.Reason = TEXT("LLM unreachable, accepting by default");
		// Story 26 — terse on the fallback PASS too. The detail still goes to
		// the log via R.Reason; the audience just hears "Pass."
		SpeakAloud(TEXT("Pass."));
		OnComplete.ExecuteIfBound(R);
		return;
	}

	UE_LOG(LogStation, Log, TEXT("Checker raw response: %s"), *Response);

	FString JsonStr;
	TSharedPtr<FJsonObject> Root;
	if (ExtractJsonObject(Response, JsonStr))
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
		FJsonSerializer::Deserialize(Reader, Root);
	}

	FString Verdict, Reason, SendBack;
	if (Root.IsValid())
	{
		Root->TryGetStringField(TEXT("verdict"), Verdict);
		Root->TryGetStringField(TEXT("reason"), Reason);
		Root->TryGetStringField(TEXT("send_back_to"), SendBack);
	}
	else
	{
		UE_LOG(LogStation, Warning,
			TEXT("Checker JSON parse failed; defaulting to accept. Raw: %s"), *Response);
	}

	// Treat any "pass"/"accept"/"ok" variant as accept. If verdict is missing entirely
	// (parse failure or empty field), default to accept so a flaky LLM reply doesn't
	// reject an otherwise-valid bucket.
	const FString VLower = Verdict.ToLower();
	const bool bExplicitReject =
		VLower.Contains(TEXT("reject")) || VLower.Contains(TEXT("fail"));
	R.bAccepted = !bExplicitReject;
	R.Reason = Reason.IsEmpty() ? TEXT("(no reason)") : Reason;
	if (SendBack.Equals(TEXT("Sorter"), ESearchCase::IgnoreCase))
	{
		R.SendBackTo = EStationType::Sorter;
	}
	else
	{
		R.SendBackTo = EStationType::Filter;
	}

	// SpeakAloud → macOS `say`. The verdict path bypasses
	// AgentChatSubsystem::HandleClaudeResponse, so without this the Checker
	// would silently flash red while the audience waits for an explanation.
	// Story 26 — PASS is terse ("Pass."); REJECT keeps the verbose reason
	// so the audience hears WHAT went wrong and which station to blame.
	if (R.bAccepted)
	{
		SpeakAloud(TEXT("Pass."));
	}
	else
	{
		SpeakAloud(FString::Printf(TEXT("[REJECT] %s"), *R.Reason));
	}
	OnComplete.ExecuteIfBound(R);
}
