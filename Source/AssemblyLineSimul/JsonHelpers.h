#pragma once

#include "CoreMinimal.h"

namespace AssemblyLineJson
{
	// Extracts the smallest balanced {...} substring from a possibly chatty LLM response.
	// Claude sometimes wraps JSON in prose or ```json fences even when asked not to.
	inline bool ExtractJsonObject(const FString& Response, FString& OutJson)
	{
		const int32 Start = Response.Find(TEXT("{"));
		if (Start == INDEX_NONE) return false;
		int32 Depth = 0;
		for (int32 i = Start; i < Response.Len(); ++i)
		{
			const TCHAR C = Response[i];
			if (C == TEXT('{')) ++Depth;
			else if (C == TEXT('}'))
			{
				if (--Depth == 0)
				{
					OutJson = Response.Mid(Start, i - Start + 1);
					return true;
				}
			}
		}
		return false;
	}
}
