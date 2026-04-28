# Story 27 — Externalize agent prompts to `.md` files

Drafted 2026-04-28.

## Goal

Move every Claude-bound prompt template out of C++ string literals
and into editable Markdown files under `Content/Agents/`. Operators
and prompt-tuners edit the wording without recompiling; reviewers
read the prompts in one place; revising a prompt becomes a clean
diff on a `.md` file instead of a noisy `.cpp` diff.

Behavior must be **byte-identical** to the current hardcoded prompts
on first commit — this story is a pure refactor, not a wording
change.

## File layout

```
Content/Agents/
├── Generator.md
├── Filter.md
├── Sorter.md
├── Checker.md         (also includes the derived-rule template)
└── ChatPrompt.md      (shared chat-template + variable contract)
```

Section markers within each agent `.md`:

```markdown
## DefaultRule
<the station's default CurrentRule>

## Role
<the chat role description>

## ProcessBucketPrompt
<the per-cycle prompt template, with {{vars}}>
```

`Checker.md` adds:

```markdown
## DerivedRuleTemplate
<the upstream-rule composition template, with {{generator_rule}} etc>
```

`ChatPrompt.md` carries:

```markdown
## ChatPromptTemplate
<the chat prompt template, with {{agent}}, {{role}}, {{rule}}, {{bucket}}, {{history}}, {{message}}>
```

## Variable substitution

Templates use `{{name}}` placeholders. The loader replaces them
verbatim with values from a `TMap<FString, FString>`. Unresolved
placeholders are left in the output (and log a warning) so missing
substitutions are obvious in the rendered prompt rather than
silently empty.

| Variable | Used in | Source |
|---|---|---|
| `{{rule}}` | every ProcessBucketPrompt + ChatPromptTemplate | `EffectiveRule` (Checker) or `CurrentRule` (others) |
| `{{input}}` | Filter / Sorter ProcessBucketPrompt | `Bucket->GetContentsString()` |
| `{{bucket}}` | Checker ProcessBucketPrompt + ChatPromptTemplate | `Bucket->GetContentsString()` |
| `{{agent}}` | ChatPromptTemplate | Station name |
| `{{role}}` | ChatPromptTemplate | Loaded from agent's `## Role` section |
| `{{history}}` | ChatPromptTemplate | Per-agent chat history block |
| `{{message}}` | ChatPromptTemplate | The new operator message |
| `{{generator_rule}}` / `{{filter_rule}}` / `{{sorter_rule}}` | Checker DerivedRuleTemplate | `CurrentRule` of the named upstream station |

## Acceptance Criteria

- **AC27.1** — Create the 5 `.md` files. Section bodies for each
  agent match the current hardcoded strings byte-for-byte (verified
  by the existing `StationSpec` Checker tests + `FullCycleFunctionalTest`
  passing without modification).

- **AC27.2** — New module `AgentPromptLibrary` (free functions in a
  namespace; no subsystem) exposes:
  - `FString LoadAgentSection(EStationType Agent, const FString& SectionName)`
  - `FString LoadChatSection(const FString& SectionName)`
  - `FString FormatPrompt(FString Template, const TMap<FString, FString>& Vars)`

  Internal cache: each `.md` file is read once per process and
  parsed into a `TMap<SectionName, Body>` keyed by agent.
  `LoadAgentSection` and `LoadChatSection` are read-through.

- **AC27.3** — `StationSubclasses.cpp`:
  - Each `ProcessBucket` builds its prompt via
    `LoadAgentSection(Type, "ProcessBucketPrompt")` +
    `FormatPrompt(...)` instead of `FString::Printf(TEXT(...))`.
  - Each station constructor loads `CurrentRule` from
    `LoadAgentSection(Type, "DefaultRule")` instead of a TEXT
    literal.
  - `ACheckerStation::GetEffectiveRule` (derived path) loads
    `LoadAgentSection(Checker, "DerivedRuleTemplate")` and
    substitutes `{{generator_rule}}` / `{{filter_rule}}` /
    `{{sorter_rule}}`.

- **AC27.4** — `AgentChatSubsystem.cpp`:
  - `BuildPromptForStation` loads
    `LoadChatSection("ChatPromptTemplate")` and substitutes the
    full variable set.
  - `GetRoleDescription(Type)` returns
    `LoadAgentSection(Type, "Role")`.

- **AC27.5** — `Config/DefaultGame.ini` adds
  `+DirectoriesToAlwaysStageAsNonUFS=(Path="Agents")` so the `.md`
  files ship in packaged builds. (Reuses the same staging
  mechanism as `Secrets/`.)

- **AC27.6** — Tests:
  - **New `AgentPromptLibrarySpec`**:
    - `FormatPrompt("hello {{name}}", {{"name", "world"}})`
      returns `"hello world"`.
    - `FormatPrompt` leaves unresolved placeholders intact and
      logs a `Warning`.
    - `LoadAgentSection(Generator, "DefaultRule")` returns the
      expected default rule (asserts on the exact current default
      to lock the file format).
    - `LoadAgentSection(Generator, "MissingSection")` returns
      empty string + logs `Warning`.
  - **All existing specs pass unchanged** — prompts are
    byte-identical to before, so `StationSpec`, `BucketSpec`,
    `AgentChatSubsystemSpec`, `FullCycleFunctionalTest`, etc.,
    require no edits.

- **AC27.7** — `Docs/Agent_Instructions.md` updated: every code-link
  pointer that currently goes to a `StationSubclasses.cpp` line is
  replaced with a pointer to the relevant `.md` section. The doc
  itself becomes much shorter (just describes structure + variables;
  the prompts themselves live in the source-of-truth `.md` files).

## Out of scope

- **Hot-reload during a single PIE session** — the cache is
  populated on first read and held for process lifetime. Editing a
  `.md` requires restarting PIE to pick up the change. (A console
  command `Agents.ReloadPrompts` could be added later if useful.)
- **Per-environment prompt variants** (dev/staging/prod) —
  single source of truth.
- **Markdown rendering** — the loader treats `## SectionName` as a
  literal section marker; everything else is plain text. No actual
  Markdown formatting is parsed or stripped.
- **YAML frontmatter** — sections are flat headings only.
- **Tuning the prompts** — wording stays byte-identical in this
  story. Wording changes happen in follow-up stories that touch
  only the `.md` files.

## Why this story matters

- **Editability** — designers / prompt engineers can A/B test
  wording without touching C++.
- **Diff hygiene** — a prompt revision is a one-file `.md` PR,
  not a 30-line `FString::Printf` rewrite.
- **Single source of truth** — the `Docs/Agent_Instructions.md`
  reference doc and the runtime prompts can't drift apart, because
  the doc just points at the `.md` files.
- **Foundation for follow-ups** — the planned "no silent agents"
  story (HTTP timeout + StopSpeaking grace window + maybe a
  `MaxTokens` bump) becomes much smaller without 8 prompt blocks
  cluttering the diff.
