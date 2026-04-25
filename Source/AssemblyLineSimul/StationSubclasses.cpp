#include "StationSubclasses.h"
#include "Bucket.h"
#include "ClaudeAPISubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"

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
}

// ---- Generator ----------------------------------------------------------------

AGeneratorStation::AGeneratorStation()
{
	StationType = EStationType::Generator;
	DisplayName = TEXT("GENERATOR");
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

	Bucket->Contents.Reset(BucketSize);
	for (int32 i = 0; i < BucketSize; ++i)
	{
		Bucket->Contents.Add(FMath::RandRange(MinValue, MaxValue));
	}
	Bucket->RefreshContents();
	SpeakStreaming(FString::Printf(TEXT("Generated %d random numbers in [%d, %d]"),
		BucketSize, MinValue, MaxValue));

	// Hold for the full per-station duration before letting the worker move on, so the cinematic
	// (which just zoomed in on OnContentsRevealed) has time to show the freshly-filled bucket
	// for as long as other stations show their work.
	UWorld* W = GetWorld();
	if (!W)
	{
		FStationProcessResult R; R.bAccepted = true;
		OnComplete.ExecuteIfBound(R);
		return;
	}
	FStationProcessComplete CapturedCompletion = OnComplete;
	FTimerHandle Th;
	W->GetTimerManager().SetTimer(Th,
		FTimerDelegate::CreateLambda([CapturedCompletion]()
		{
			FStationProcessResult R; R.bAccepted = true;
			CapturedCompletion.ExecuteIfBound(R);
		}),
		20.0f, false);
}

// ---- Filter (primes) ----------------------------------------------------------

AFilterStation::AFilterStation()
{
	StationType = EStationType::Filter;
	DisplayName = TEXT("FILTER (PRIMES)");
	ErrorRate = 0.15f;
}

void AFilterStation::ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete)
{
	if (!Bucket)
	{
		FStationProcessResult R; R.bAccepted = false; R.Reason = TEXT("Null bucket");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	TArray<int32> Filtered;
	Filtered.Reserve(Bucket->Contents.Num());
	for (int32 N : Bucket->Contents)
	{
		const bool bPrime = IsPrime(N);
		// "Mistake": occasionally let a non-prime through.
		const bool bMistake = !bPrime && FMath::FRand() < ErrorRate;
		if (bPrime || bMistake)
		{
			Filtered.Add(N);
		}
	}
	const int32 KeptCount = Filtered.Num();
	Bucket->Contents = MoveTemp(Filtered);
	Bucket->RefreshContents();
	SpeakStreaming(FString::Printf(TEXT("Kept %d primes"), KeptCount));

	FStationProcessResult R; R.bAccepted = true;
	OnComplete.ExecuteIfBound(R);
}

// ---- Sorter -------------------------------------------------------------------

ASorterStation::ASorterStation()
{
	StationType = EStationType::Sorter;
	DisplayName = TEXT("SORTER");
	ErrorRate = 0.15f;
}

void ASorterStation::ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete)
{
	if (!Bucket)
	{
		FStationProcessResult R; R.bAccepted = false; R.Reason = TEXT("Null bucket");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	Bucket->Contents.Sort();

	// "Mistake": occasionally swap two adjacent values after sorting.
	if (Bucket->Contents.Num() >= 2 && FMath::FRand() < ErrorRate)
	{
		const int32 Idx = FMath::RandRange(0, Bucket->Contents.Num() - 2);
		Bucket->Contents.Swap(Idx, Idx + 1);
	}
	Bucket->RefreshContents();
	SpeakStreaming(TEXT("Sorted ascending"));

	FStationProcessResult R; R.bAccepted = true;
	OnComplete.ExecuteIfBound(R);
}

// ---- Checker (LLM) ------------------------------------------------------------

ACheckerStation::ACheckerStation()
{
	StationType = EStationType::Checker;
	DisplayName = TEXT("CHECKER (LLM)");
	if (NameLabel) NameLabel->SetTextRenderColor(FColor::Magenta);
}

void ACheckerStation::ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete)
{
	if (!Bucket)
	{
		FStationProcessResult R; R.bAccepted = false; R.Reason = TEXT("Null bucket");
		OnComplete.ExecuteIfBound(R);
		return;
	}

	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UClaudeAPISubsystem* Claude = GI ? GI->GetSubsystem<UClaudeAPISubsystem>() : nullptr;

	const FString Numbers = Bucket->GetContentsString();
	const FString Prompt = FString::Printf(
		TEXT("You are the QA agent on an assembly line. The Filter station should remove all non-primes; ")
		TEXT("the Sorter station should sort the remaining primes ascending. ")
		TEXT("Verify this bucket meets ALL THREE conditions: (1) every value is prime, (2) values are sorted ")
		TEXT("strictly ascending, (3) every value is in range 1-100.\n\n")
		TEXT("Bucket: %s\n\n")
		TEXT("Respond with ONLY a JSON object on a single line, no markdown:\n")
		TEXT("{\"verdict\":\"pass\"|\"reject\",\"reason\":\"...\",\"send_back_to\":\"Filter\"|\"Sorter\"|null}\n")
		TEXT("If verdict is pass, send_back_to MUST be null. ")
		TEXT("If non-primes are present, send back to Filter. ")
		TEXT("If only ordering is wrong, send back to Sorter. ")
		TEXT("The 'reason' must be ONE plain-English sentence (no jargon, no JSON-speak) ")
		TEXT("that an audience can read on a screen — name specific offending numbers if relevant. ")
		TEXT("Keep reason under 140 characters."),
		*Numbers);

	SpeakStreaming(FString::Printf(TEXT("Inspecting bucket: %s"), *Numbers));

	if (!Claude)
	{
		// Fallback: deterministic local check so the demo still runs without an API key.
		FStationProcessResult R; R.bAccepted = true;
		bool bAllPrime = true, bSorted = true, bInRange = true;
		for (int32 i = 0; i < Bucket->Contents.Num(); ++i)
		{
			const int32 N = Bucket->Contents[i];
			if (N < 1 || N > 100) bInRange = false;
			if (!IsPrime(N)) bAllPrime = false;
			if (i > 0 && Bucket->Contents[i - 1] >= N) bSorted = false;
		}
		if (!bAllPrime) { R.bAccepted = false; R.Reason = TEXT("Non-prime present"); R.SendBackTo = EStationType::Filter; }
		else if (!bSorted) { R.bAccepted = false; R.Reason = TEXT("Out of order"); R.SendBackTo = EStationType::Sorter; }
		else if (!bInRange) { R.bAccepted = false; R.Reason = TEXT("Out of range"); R.SendBackTo = EStationType::Filter; }
		else { R.Reason = TEXT("All checks passed"); }
		SpeakStreaming(FString::Printf(TEXT("%s: %s"),
			R.bAccepted ? TEXT("PASS") : TEXT("REJECT"), *R.Reason));
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

			// Naive JSON extraction (the prompt asks for one-line JSON).
			FString Verdict, Reason, SendBack;
			auto Extract = [&](const FString& Key, FString& Out)
			{
				const FString Needle = FString::Printf(TEXT("\"%s\""), *Key);
				int32 KeyIdx = Response.Find(Needle);
				if (KeyIdx == INDEX_NONE) return;
				int32 Colon = Response.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromStart, KeyIdx);
				if (Colon == INDEX_NONE) return;
				int32 Q1 = Response.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, Colon);
				if (Q1 == INDEX_NONE) return;
				int32 Q2 = Response.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, Q1 + 1);
				if (Q2 == INDEX_NONE) return;
				Out = Response.Mid(Q1 + 1, Q2 - Q1 - 1);
			};
			Extract(TEXT("verdict"), Verdict);
			Extract(TEXT("reason"), Reason);
			Extract(TEXT("send_back_to"), SendBack);

			R.bAccepted = Verdict.Equals(TEXT("pass"), ESearchCase::IgnoreCase);
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
