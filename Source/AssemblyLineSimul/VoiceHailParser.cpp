#include "VoiceHailParser.h"

namespace
{
	// Replace any non-alphanumeric character with a space so punctuation collapses to
	// word separators. Keeps tokenisation trivial.
	FString StripPunctuation(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (TCHAR C : In)
		{
			Out.AppendChar((FChar::IsAlnum(C) || FChar::IsWhitespace(C)) ? C : TEXT(' '));
		}
		return Out;
	}

	int32 LevenshteinDistance(const FString& A, const FString& B)
	{
		const int32 M = A.Len();
		const int32 N = B.Len();
		if (M == 0) return N;
		if (N == 0) return M;
		TArray<int32> Prev, Curr;
		Prev.SetNum(N + 1);
		Curr.SetNum(N + 1);
		for (int32 j = 0; j <= N; ++j) Prev[j] = j;
		for (int32 i = 1; i <= M; ++i)
		{
			Curr[0] = i;
			for (int32 j = 1; j <= N; ++j)
			{
				const int32 Cost = (A[i - 1] == B[j - 1]) ? 0 : 1;
				Curr[j] = FMath::Min3(Prev[j] + 1, Curr[j - 1] + 1, Prev[j - 1] + Cost);
			}
			Prev = Curr;
		}
		return Prev[N];
	}

	bool MatchesAgent(const FString& Token, EStationType& OutStation)
	{
		struct FAgent { const TCHAR* Name; EStationType Type; };
		static const FAgent Agents[] = {
			{ TEXT("generator"), EStationType::Generator },
			{ TEXT("filter"),    EStationType::Filter    },
			{ TEXT("sorter"),    EStationType::Sorter    },
			{ TEXT("checker"),   EStationType::Checker   },
		};
		// Tolerate up to 2 edits — covers Whisper letter swaps ("filtre"/"filter") and
		// dropped letters ("soter"/"sorter"). All four agent names are distinct enough
		// (≥ 4 edits apart from common non-agent words like "people" / "there") that a
		// distance-2 threshold doesn't false-match.
		for (const FAgent& A : Agents)
		{
			if (LevenshteinDistance(Token, FString(A.Name)) <= 2)
			{
				OutStation = A.Type;
				return true;
			}
		}
		return false;
	}

	bool ContainsPhrase(const TArray<FString>& Tokens, const TArray<FString>& Phrase, int32 StartFrom)
	{
		for (int32 i = StartFrom; i + Phrase.Num() <= Tokens.Num(); ++i)
		{
			bool bMatch = true;
			for (int32 j = 0; j < Phrase.Num(); ++j)
			{
				if (Tokens[i + j] != Phrase[j]) { bMatch = false; break; }
			}
			if (bMatch) return true;
		}
		return false;
	}
}

namespace AssemblyLineVoice
{
	bool TryParseHail(const FString& Transcript, EStationType& OutStation)
	{
		if (Transcript.IsEmpty()) return false;

		const FString Cleaned = StripPunctuation(Transcript).ToLower();
		TArray<FString> Tokens;
		Cleaned.ParseIntoArray(Tokens, TEXT(" "), true);
		if (Tokens.Num() < 3) return false;

		// Must start with "hey".
		if (Tokens[0] != TEXT("hey")) return false;

		// Token at index 1 should be an agent name (fuzzy match).
		EStationType Candidate = EStationType::Generator;
		if (!MatchesAgent(Tokens[1], Candidate)) return false;

		// Must contain a confirmation phrase somewhere after the agent name.
		static const TArray<TArray<FString>> Phrases = {
			{ TEXT("do"),  TEXT("you"), TEXT("read"),  TEXT("me") },
			{ TEXT("do"),  TEXT("you"), TEXT("copy") },
			{ TEXT("are"), TEXT("you"), TEXT("there") },
		};
		for (const TArray<FString>& Phrase : Phrases)
		{
			if (ContainsPhrase(Tokens, Phrase, /*StartFrom=*/2))
			{
				OutStation = Candidate;
				return true;
			}
		}
		return false;
	}
}
