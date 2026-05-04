#include "AgentPromptLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogAgentPrompts, Log, All);

namespace
{
	using FSectionMap = TMap<FString, FString>;

	// Process-lifetime caches. Each .md is read + parsed once on first use.
	static TMap<EStationType, FSectionMap> CachedAgents;
	static FSectionMap CachedChat;

	const TCHAR* GetAgentFilename(EStationType Agent)
	{
		switch (Agent)
		{
		case EStationType::Generator:    return TEXT("Generator.md");
		case EStationType::Filter:       return TEXT("Filter.md");
		case EStationType::Sorter:       return TEXT("Sorter.md");
		case EStationType::Checker:      return TEXT("Checker.md");
		case EStationType::Orchestrator: return TEXT("Orchestrator.md");
		}
		return TEXT("");
	}

	FString LoadFileFromAgentsDir(const TCHAR* Filename)
	{
		// Story 33b — Saved/Agents/ wins over Content/Agents/ so the
		// Orchestrator's per-mission writes shadow the static templates.
		// Mirrors the LoadAPIKey precedence pattern.
		const FString SavedPath = FPaths::ProjectSavedDir() / TEXT("Agents") / Filename;
		FString Content;
		if (FFileHelper::LoadFileToString(Content, *SavedPath))
		{
			return Content;
		}

		const FString ContentPath = FPaths::ProjectContentDir() / TEXT("Agents") / Filename;
		if (!FFileHelper::LoadFileToString(Content, *ContentPath))
		{
			UE_LOG(LogAgentPrompts, Warning,
				TEXT("AgentPromptLibrary: failed to load %s (also missing in %s)"),
				*ContentPath, *SavedPath);
		}
		return Content;
	}

	// Parses a markdown blob into { sectionName -> body } where each section
	// header is "## <SectionName>" at the start of a line. Body is everything
	// between the header and the next "## " header (or end of file), with
	// leading and trailing whitespace stripped.
	FSectionMap ParseSections(const FString& Content)
	{
		FSectionMap Out;
		const FString Marker = TEXT("## ");
		int32 Pos = 0;

		// Find the first heading
		auto FindHeadingFrom = [&Content, &Marker](int32 From) -> int32
		{
			while (From <= Content.Len() - Marker.Len())
			{
				const int32 Found = Content.Find(Marker, ESearchCase::CaseSensitive,
					ESearchDir::FromStart, From);
				if (Found == INDEX_NONE) return INDEX_NONE;
				// Must be at start of file or preceded by '\n'
				if (Found == 0 || Content[Found - 1] == TEXT('\n'))
				{
					return Found;
				}
				From = Found + Marker.Len();
			}
			return INDEX_NONE;
		};

		Pos = FindHeadingFrom(0);
		while (Pos != INDEX_NONE)
		{
			const int32 NameStart = Pos + Marker.Len();
			int32 NameEnd = Content.Find(TEXT("\n"), ESearchCase::CaseSensitive,
				ESearchDir::FromStart, NameStart);
			if (NameEnd == INDEX_NONE) NameEnd = Content.Len();

			FString SectionName = Content.Mid(NameStart, NameEnd - NameStart).TrimStartAndEnd();

			const int32 BodyStart = (NameEnd < Content.Len()) ? NameEnd + 1 : Content.Len();
			const int32 NextHead = FindHeadingFrom(BodyStart);
			const int32 BodyEnd = (NextHead != INDEX_NONE) ? NextHead : Content.Len();

			FString Body = Content.Mid(BodyStart, BodyEnd - BodyStart).TrimStartAndEnd();
			Out.Add(MoveTemp(SectionName), MoveTemp(Body));

			Pos = NextHead;
		}

		return Out;
	}
}

namespace AgentPromptLibrary
{
	FString LoadAgentSection(EStationType Agent, const FString& SectionName)
	{
		FSectionMap& Sections = CachedAgents.FindOrAdd(Agent);
		if (Sections.Num() == 0)
		{
			Sections = ParseSections(LoadFileFromAgentsDir(GetAgentFilename(Agent)));
		}
		if (const FString* Body = Sections.Find(SectionName))
		{
			return *Body;
		}
		UE_LOG(LogAgentPrompts, Warning,
			TEXT("AgentPromptLibrary: section '%s' not found in %s"),
			*SectionName, GetAgentFilename(Agent));
		return FString();
	}

	FString LoadChatSection(const FString& SectionName)
	{
		if (CachedChat.Num() == 0)
		{
			CachedChat = ParseSections(LoadFileFromAgentsDir(TEXT("ChatPrompt.md")));
		}
		if (const FString* Body = CachedChat.Find(SectionName))
		{
			return *Body;
		}
		UE_LOG(LogAgentPrompts, Warning,
			TEXT("AgentPromptLibrary: section '%s' not found in ChatPrompt.md"),
			*SectionName);
		return FString();
	}

	void InvalidateCache()
	{
		CachedAgents.Reset();
		CachedChat.Reset();
	}

	FString FormatPrompt(FString Template, const TMap<FString, FString>& Vars)
	{
		for (const TPair<FString, FString>& Var : Vars)
		{
			const FString Placeholder = FString::Printf(TEXT("{{%s}}"), *Var.Key);
			Template = Template.Replace(*Placeholder, *Var.Value, ESearchCase::CaseSensitive);
		}

		// Surface the first unresolved placeholder so misses don't go silently into
		// the model. The output keeps the placeholder verbatim — Claude will see
		// "{{somevar}}" in the prompt rather than empty space.
		const int32 Open = Template.Find(TEXT("{{"));
		if (Open != INDEX_NONE)
		{
			const int32 Close = Template.Find(TEXT("}}"),
				ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
			if (Close != INDEX_NONE)
			{
				const FString Unresolved = Template.Mid(Open, Close - Open + 2);
				UE_LOG(LogAgentPrompts, Warning,
					TEXT("AgentPromptLibrary: unresolved placeholder %s in prompt"),
					*Unresolved);
			}
		}
		return Template;
	}
}
