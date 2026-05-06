# Agent instructions reference

Every prompt the demo sends to Claude on behalf of an agent lives
as authoritative `.md` source under `Content/Agents/`. This doc is
a thin pointer at the files; the prompts themselves are no longer
duplicated here, so they cannot drift.

> **Story 38 vocabulary note.** The `.md` placeholders kept their
> historic names (`{{input}}`, `{{bucket}}`) and the
> `ProcessBucketPrompt` section name is unchanged — agent prompts
> are operator-facing prose where the colloquial word "bucket"
> reads naturally even though the runtime type is now
> `APayloadCarrier`. The carrier's `GetContentsString()` is a thin
> pass-through that delegates to `Payload->ToPromptString()`. See
> README § "Payload + Carrier abstraction (Story 38 deep dive)".

The runtime loader is `AgentPromptLibrary` ([header](../Source/AssemblyLineSimul/AgentPromptLibrary.h),
[cpp](../Source/AssemblyLineSimul/AgentPromptLibrary.cpp)) — a
namespace of free functions. It reads each `.md` file once per
process, parses it into `## SectionName → body`, and substitutes
`{{name}}` placeholders against a `TMap`. Process-lifetime cache;
restart PIE to pick up `.md` edits.

---

## At a glance

The demo sends Claude **two completely separate families** of
prompts. Neither sees the other's history.

| Family | Trigger | Volume | History? | Schema |
|---|---|---|---|---|
| **Per-cycle `ProcessBucket`** | Worker enters Working state at a station's dock | 4 calls per cycle (one per station) | Stateless | `{"result": [...]}` or `{"verdict": ..., "reason": ..., "send_back_to": ...}` |
| **Chat** | Operator hails an agent via push-to-talk | 1 call per operator message | Per-agent turn-by-turn history accumulated in payload | `{"reply": "...", "new_rule": "..." \| null}` |

**No system prompt.** Both families pack everything into the user
message; the Anthropic `system:` field is never set. **No tool use.**
Claude returns strict JSON; we parse with
`JsonHelpers::ExtractJsonObject`. **No prompt caching.** Every
request is a fresh body.

---

## File layout

```
Content/Agents/
├── Generator.md      ← DefaultRule, Role, ProcessBucketPrompt
├── Filter.md         ← DefaultRule, Role, ProcessBucketPrompt
├── Sorter.md         ← DefaultRule, Role, ProcessBucketPrompt
├── Checker.md        ← DefaultRule, Role, DerivedRuleTemplate, ProcessBucketPrompt
└── ChatPrompt.md     ← ChatPromptTemplate (shared across all agents)
```

Each agent file uses Markdown `## SectionName` headers as section
delimiters; the body is everything between one header and the next
(or end of file), with leading and trailing whitespace stripped.

## The five files

| File | Used by | Sections |
|---|---|---|
| [`Generator.md`](../Content/Agents/Generator.md) | Generator station + chat | `DefaultRule`, `Role`, `ProcessBucketPrompt` |
| [`Filter.md`](../Content/Agents/Filter.md) | Filter station + chat | `DefaultRule`, `Role`, `ProcessBucketPrompt` |
| [`Sorter.md`](../Content/Agents/Sorter.md) | Sorter station + chat | `DefaultRule`, `Role`, `ProcessBucketPrompt` |
| [`Checker.md`](../Content/Agents/Checker.md) | Checker station + chat | `DefaultRule`, `Role`, `DerivedRuleTemplate`, `ProcessBucketPrompt` |
| [`ChatPrompt.md`](../Content/Agents/ChatPrompt.md) | All chat traffic | `ChatPromptTemplate` |

## Variable reference

`AgentPromptLibrary::FormatPrompt` substitutes `{{name}}` in the
loaded template against a `TMap<FString, FString>`. Unresolved
placeholders are left intact in the output and the first one logs
a warning.

| Placeholder | Used by | Source |
|---|---|---|
| `{{rule}}` | every `ProcessBucketPrompt` + `ChatPromptTemplate` | `EffectiveRule` (Checker) or `CurrentRule` (others) |
| `{{input}}` | Filter / Sorter `ProcessBucketPrompt` | `Carrier->GetContentsString()` (Story 38 — pass-through that delegates to `Carrier->Payload->ToPromptString()`; renamed from `Bucket->GetContentsString()`) |
| `{{bucket}}` | Checker `ProcessBucketPrompt` + `ChatPromptTemplate` | `Carrier->GetContentsString()` (or `(empty)` for chat with no carrier at the dock) |
| `{{agent}}` | `ChatPromptTemplate` | Station name |
| `{{role}}` | `ChatPromptTemplate` | Loaded from agent's `## Role` section |
| `{{history}}` | `ChatPromptTemplate` | Per-agent chat history block ("Role: text\n" lines) |
| `{{message}}` | `ChatPromptTemplate` | The new operator message |
| `{{generator_rule}}` / `{{filter_rule}}` / `{{sorter_rule}}` | Checker `DerivedRuleTemplate` | `CurrentRule` of the named upstream station |

## Where each section is consumed in C++

| `.md` Section | Loaded by | Code site |
|---|---|---|
| `Generator.md / DefaultRule` | `AGeneratorStation::AGeneratorStation` | [StationSubclasses.cpp:74](../Source/AssemblyLineSimul/StationSubclasses.cpp#L74) |
| `Generator.md / ProcessBucketPrompt` | `AGeneratorStation::ProcessBucket` | [StationSubclasses.cpp](../Source/AssemblyLineSimul/StationSubclasses.cpp) |
| `Filter.md / *` | `AFilterStation` | [StationSubclasses.cpp](../Source/AssemblyLineSimul/StationSubclasses.cpp) |
| `Sorter.md / *` | `ASorterStation` | [StationSubclasses.cpp](../Source/AssemblyLineSimul/StationSubclasses.cpp) |
| `Checker.md / DerivedRuleTemplate` | `ACheckerStation::GetEffectiveRule` (when `bUseDerivedRule = true`) | [StationSubclasses.cpp](../Source/AssemblyLineSimul/StationSubclasses.cpp) |
| `Checker.md / ProcessBucketPrompt` | `ACheckerStation::ProcessBucket` | [StationSubclasses.cpp](../Source/AssemblyLineSimul/StationSubclasses.cpp) |
| `<Agent>.md / Role` | `UAgentChatSubsystem::GetRoleDescription` | [AgentChatSubsystem.cpp](../Source/AssemblyLineSimul/AgentChatSubsystem.cpp) |
| `ChatPrompt.md / ChatPromptTemplate` | `UAgentChatSubsystem::BuildPromptForStation` | [AgentChatSubsystem.cpp](../Source/AssemblyLineSimul/AgentChatSubsystem.cpp) |

## Editing prompts

1. Edit the `.md` file in `Content/Agents/`.
2. Restart PIE (the cache is process-lifetime; no live reload yet).
3. Run the spec sweep — `AgentPromptLibrarySpec` asserts on the
   exact text of `Generator.md / DefaultRule` and the `Filter.md /
   Role`, so tweaks to those two strings will fail the spec on
   purpose. If you intend the change, update the assertion in
   `AgentPromptLibrarySpec.cpp` in the same commit.

## What's NOT in the prompts

- **No `system:` prompt.** Everything is packed into the user message.
- **No tool use / function calling.** Claude is asked to emit
  strict JSON only; we parse via `JsonHelpers::ExtractJsonObject`.
- **No prompt caching.** Every request body is fresh, so long chat
  sessions grow request size linearly with history.
- **No few-shot examples** beyond the single `Example: {...}` line
  in `Generator.md / ProcessBucketPrompt`.
- **No language hint, persona, or temperature override.** The
  `UClaudeAPISubsystem` sends only `model` (`claude-sonnet-4-6`),
  `max_tokens=512`, and the user message; Anthropic defaults apply
  for everything else.

  > **Heads up:** `max_tokens=512` is a tight cap for big payloads.
  > Asking the Generator for ~100 integers can hit the cap; the
  > response truncates mid-array, JSON parsing fails, and the cycle
  > falls through the "LLM failed" branch (still completes, but
  > with an empty result). Bump `MaxTokens` in `ClaudeAPISubsystem.h`
  > if you want to support bigger asks.
