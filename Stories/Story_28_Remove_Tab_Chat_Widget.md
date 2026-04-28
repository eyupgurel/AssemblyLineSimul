# Story 28 — Remove the Tab-toggled chat widget entirely

Drafted 2026-04-28.

## Goal

Voice push-to-talk has been the production input channel since
Story 14. The Tab-toggled `UAgentChatWidget` has been dead weight
ever since — there's no design reason to keep it, and the floating
panel competes with the cinematic camera. Strip it out completely:
the class, the GameMode plumbing, the Tab keybinding, the docs
that still mention it, and the historical "kept" claims in
Stories 14 and 23.

## What's removed

### Code
- `Source/AssemblyLineSimul/AgentChatWidget.h` — whole file deleted.
- `Source/AssemblyLineSimul/AgentChatWidget.cpp` — whole file deleted.
- `AAssemblyLineGameMode`:
  - `class UAgentChatWidget;` forward decl + `#include`.
  - `ChatWidget`, `ChatToggleAction`, `ChatToggleMappingContext`
    UPROPERTYs.
  - `SpawnChatWidget()`, `ToggleChatWidget()` methods (declaration
    + impl).
  - `SpawnChatWidget()` call in `BeginPlay`.
- The Tab keybinding (`MapKey(ChatToggleAction, EKeys::Tab)`) goes
  with the rest. Tab is no longer mapped to anything in this
  project.

### Docs
- `README.md`:
  - "Press **Tab** to open the chat HUD if you'd rather type." —
    deleted.
  - System-overview mermaid `Player input` node loses "Tab chat
    toggle" — Space-only.
  - Project-layout listing drops `AgentChatWidget.{h,cpp}`.
- `Docs/Agent_Instructions.md` — at-a-glance trigger column for
  the Chat family changes from "Tab HUD or push-to-talk" to just
  "push-to-talk".

### Story docs
- `Stories/Story_14_Voice_Driven_Agent_Dialogue.md` — opening
  line "Replace the Tab-toggled chat widget as the primary input
  channel" is updated to reflect that voice is now the **only**
  channel (chat widget removed in Story 28).
- `Stories/Story_23_Strip_InWorld_Text.md` — the lines that
  explicitly KEPT the Tab HUD chat get a strikethrough + "removed
  in Story 28" annotation, so the historical record stays
  accurate.

## What stays

- `UAgentChatSubsystem` is **kept** in full. It's the production
  business-logic path for chat: voice transcripts route through
  `SendMessage` → Claude → `SpeakResponse`. The widget was just
  one (now-unused) entry point to that path.
- `UAgentChatSubsystem::SpeakResponse` and `StopSpeaking` (Story
  26) keep their behavior.
- All tests on `AgentChatSubsystem` (history, prompt construction,
  `OnRuleUpdated`, `StopSpeaking`) still pass — they never touched
  the widget.

## Acceptance Criteria

- **AC28.1** — `Source/AssemblyLineSimul/AgentChatWidget.h` and
  `.cpp` are deleted from disk + git.

- **AC28.2** — `AAssemblyLineGameMode.h/cpp` no longer references
  `UAgentChatWidget`, `ChatWidget`, `ChatToggleAction`,
  `ChatToggleMappingContext`, `SpawnChatWidget`, or
  `ToggleChatWidget` — all symbols removed; `BeginPlay` no longer
  calls `SpawnChatWidget()`.

- **AC28.3** — `git grep -nE 'AgentChatWidget|ChatToggle|SpawnChatWidget|ToggleChatWidget'`
  in `Source/` returns zero hits. Same grep in `Stories/` and
  `Docs/` returns zero non-historical hits (only the Story
  28 doc itself or annotated/strikethrough historical mentions).

- **AC28.4** — Build is green and the existing 79 specs pass
  unchanged. No new specs are added — this story is pure deletion.

- **AC28.5** — README, `Docs/Agent_Instructions.md`, Story 14
  intro, and Story 23's "what stays" section are all updated as
  described above.

## Out of scope

- Removing `UAgentChatSubsystem` itself — it's load-bearing for
  voice.
- Removing the Tab key from any other future binding — this story
  just unbinds Tab from the chat widget; if Tab is later wanted
  for something else, that's a separate story.
- Adding a replacement HUD — voice is the only input channel
  going forward.
