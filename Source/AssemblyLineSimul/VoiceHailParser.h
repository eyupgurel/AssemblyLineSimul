#pragma once

#include "CoreMinimal.h"
#include "AssemblyLineTypes.h"

namespace AssemblyLineVoice
{
	// Tries to interpret a transcript as a "hailing" utterance addressed to a specific
	// agent — e.g. "Hey Filter, do you read me?". Returns true and fills OutStation when
	// the transcript starts with `hey`, contains an agent name, and ends with a phrase
	// like "do you read me" / "do you copy" / "are you there". Tolerant of punctuation
	// and surrounding filler, case-insensitive, and accepts close misspellings of the
	// agent name (e.g. "filtre", "soter").
	bool TryParseHail(const FString& Transcript, EStationType& OutStation);
}
