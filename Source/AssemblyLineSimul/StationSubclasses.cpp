#include "StationSubclasses.h"
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
	CurrentRule = TEXT("Generate 10 random integers in the range 1 to 100");
	if (NameLabel) NameLabel->SetTextRenderColor(FColor::Green);
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
		SpeakStreaming(TEXT("LLM unreachable — generator has no rule engine to fall back on."));
		FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM unreachable");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	const FString Prompt = FString::Printf(
		TEXT("You are the Generator agent on an assembly line. Apply this rule to produce a fresh ")
		TEXT("bucket of integers:\n\n")
		TEXT("RULE: %s\n\n")
		TEXT("Respond with ONLY a JSON object on a single line, no markdown:\n")
		TEXT("{\"result\":[<integers>]}\n")
		TEXT("Example: {\"result\":[3,17,42,7,91]}"),
		*CurrentRule);

	SpeakStreaming(FString::Printf(TEXT("Generating per rule: %s"), *CurrentRule));

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
				Self->SpeakStreaming(FString::Printf(TEXT("LLM failed: %s"), *Response));
				FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM failed");
				OnComplete.ExecuteIfBound(R);
				return;
			}

			B->Contents = MoveTemp(Numbers);
			B->RefreshContents();
			Self->SpeakStreaming(FString::Printf(TEXT("Generated: %s"), *B->GetContentsString()));

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
	CurrentRule = TEXT("Keep only the prime numbers; remove non-primes.");
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
		SpeakStreaming(TEXT("LLM unreachable — passing bucket through unchanged."));
		FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM unreachable");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	const FString Input = Bucket->GetContentsString();
	const FString Prompt = FString::Printf(
		TEXT("You are the Filter agent on an assembly line. Apply this rule to the input bucket ")
		TEXT("and return the filtered bucket. Preserve the original order of the kept items.\n\n")
		TEXT("RULE: %s\n")
		TEXT("INPUT: [%s]\n\n")
		TEXT("Respond with ONLY a JSON object on a single line, no markdown:\n")
		TEXT("{\"result\":[<integers>]}"),
		*CurrentRule, *Input);

	SpeakStreaming(FString::Printf(TEXT("Filtering [%s] per rule: %s"), *Input, *CurrentRule));

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
				Self->SpeakStreaming(FString::Printf(TEXT("LLM failed: %s"), *Response));
				FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM failed");
				OnComplete.ExecuteIfBound(R);
				return;
			}

			B->Contents = MoveTemp(Numbers);
			B->RefreshContents();
			Self->SpeakStreaming(FString::Printf(TEXT("Kept: %s"), *B->GetContentsString()));

			FStationProcessResult R; R.bAccepted = true;
			OnComplete.ExecuteIfBound(R);
		}));
}

// ---- Sorter -------------------------------------------------------------------

ASorterStation::ASorterStation()
{
	StationType = EStationType::Sorter;
	DisplayName = TEXT("SORTER");
	CurrentRule = TEXT("Sort the numbers in strictly ascending order.");
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
		SpeakStreaming(TEXT("LLM unreachable — passing bucket through unchanged."));
		FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM unreachable");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	const FString Input = Bucket->GetContentsString();
	const FString Prompt = FString::Printf(
		TEXT("You are the Sorter agent on an assembly line. Apply this rule to the input bucket ")
		TEXT("and return the reordered bucket (do not add or remove values, only reorder).\n\n")
		TEXT("RULE: %s\n")
		TEXT("INPUT: [%s]\n\n")
		TEXT("Respond with ONLY a JSON object on a single line, no markdown:\n")
		TEXT("{\"result\":[<integers>]}"),
		*CurrentRule, *Input);

	SpeakStreaming(FString::Printf(TEXT("Sorting [%s] per rule: %s"), *Input, *CurrentRule));

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
				Self->SpeakStreaming(FString::Printf(TEXT("LLM failed: %s"), *Response));
				FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM failed");
				OnComplete.ExecuteIfBound(R);
				return;
			}

			B->Contents = MoveTemp(Numbers);
			B->RefreshContents();
			Self->SpeakStreaming(FString::Printf(TEXT("Sorted: %s"), *B->GetContentsString()));

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
	CurrentRule = TEXT("The bucket should contain only prime numbers in [1, 100], sorted strictly ascending.");
	if (NameLabel) NameLabel->SetTextRenderColor(FColor::Magenta);
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

	return FString::Printf(
		TEXT("The bucket has been processed by three upstream agents in this order:\n")
		TEXT("  1. Generator produced items per: %s\n")
		TEXT("  2. Filter then applied: %s\n")
		TEXT("  3. Sorter then applied: %s\n")
		TEXT("Verify the final bucket is consistent with the chain of rules. ")
		TEXT("On reject: send back to 'Filter' if the wrong items are present, ")
		TEXT("or 'Sorter' if items are in the wrong order."),
		*RuleOf(EStationType::Generator),
		*RuleOf(EStationType::Filter),
		*RuleOf(EStationType::Sorter));
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
	const FString Prompt = FString::Printf(
		TEXT("You are the QA / Checker agent on an assembly line. Verify the bucket against your rule.\n\n")
		TEXT("RULE: %s\n")
		TEXT("BUCKET: %s\n\n")
		TEXT("Be conservative: ACCEPT unless you can name a SPECIFIC offending item ")
		TEXT("(e.g. '9 is not prime') or a SPECIFIC out-of-order pair ")
		TEXT("(e.g. '17 comes before 11'). Bucket size alone is not a reason to reject.\n\n")
		TEXT("Respond with ONLY a JSON object on a single line, no markdown:\n")
		TEXT("{\"verdict\":\"pass\"|\"reject\",\"reason\":\"...\",\"send_back_to\":\"Filter\"|\"Sorter\"|null}\n")
		TEXT("On reject: send_back_to is 'Filter' for content errors, 'Sorter' for ordering errors. ")
		TEXT("On pass: send_back_to MUST be null.\n")
		TEXT("'reason' is ONE plain-English sentence (no JSON-speak) under 140 characters."),
		*GetEffectiveRule(), *Numbers);

	SpeakStreaming(FString::Printf(TEXT("Inspecting bucket: %s"), *Numbers));

	if (!Claude)
	{
		FStationProcessResult R; R.bAccepted = true; R.Reason = TEXT("LLM unreachable, accepting by default");
		SpeakStreaming(R.Reason);
		OnComplete.ExecuteIfBound(R);
		return;
	}

	TWeakObjectPtr<ACheckerStation> WeakThis(this);
	Claude->SendMessage(Prompt,
		FClaudeComplete::CreateLambda([WeakThis, OnComplete](bool bSuccess, const FString& Response)
		{
			FStationProcessResult R;
			if (!bSuccess)
			{
				R.bAccepted = true;
				R.Reason = TEXT("LLM unreachable, accepting by default");
				if (ACheckerStation* Self = WeakThis.Get())
				{
					Self->SpeakStreaming(FString::Printf(TEXT("LLM unreachable — %s"), *Response));
				}
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

			if (ACheckerStation* Self = WeakThis.Get())
			{
				Self->SpeakStreaming(FString::Printf(TEXT("[%s] %s"),
					R.bAccepted ? TEXT("PASS") : TEXT("REJECT"), *R.Reason));
			}
			OnComplete.ExecuteIfBound(R);
		}));
}
