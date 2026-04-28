# Story 23 — Strip in-world text labels and talk widgets

Drafted 2026-04-28.

## Goal

Remove every piece of text rendered in the 3D world so the audience
sees workers, buckets, and floor with nothing obscuring them. The
2D HUD chat (Tab toggle) and operational on-screen debug messages
stay — only **in-world floating labels and talk panels** are removed.

## What's removed

- **`AStation::NameLabel`** (`UTextRenderComponent`) — the floating
  "GENERATOR / FILTER / SORTER / CHECKER" text above each station.
- **`AStation::TalkWidgetComponent`** (in-world UMG panel) plus the
  `UStationTalkWidget` class itself (`StationTalkWidget.h/cpp` and the
  orphan `WBP_StationTalkPanel_Holo.uasset`) — the in-world panel that
  used to surface the Checker's verdict and the active-agent
  affirmations. **The TTS audio path through `Chat->SpeakResponse`
  stays**, so the audience still HEARS the verdict / affirmation.
- **`AStation::Speak / SpeakStreaming / ClearTalk / GetTalkWidget`** —
  panel-write APIs go. `SpeakAloud` is reduced to the TTS-only path.
- **`AStation::BillboardLabel`** plus its calls in `Tick` (no labels
  left to billboard).
- **`AAssemblyLineGameMode::StationTalkWidgetClass`** UPROPERTY (no
  consumer after the AStation change).
- **`ABucket::NumberBallLabels`** (`TArray<UTextRenderComponent>`) +
  the label-construction block in `RefreshContents` + the
  billboard-on-labels tick. Painted-texture numbers on the ball
  surfaces (`BilliardBallMaterial` path) are **kept** — those are
  data viz, not floating text.
- **`AWorkerRobot::StateLabel`** (`UTextRenderComponent`) + any
  update / billboard call paths — the floating "Idle / Working /
  Carrying" text above each worker.

## What's kept

- **`UAgentChatWidget`** (HUD chat input toggled with Tab) — interactive
  input, not decorative text.
- **On-screen debug messages** ("● REC", "Transcribing…", transcripts) —
  operational signal.
- **`Chat->SpeakResponse` / macOS-`say` TTS pipeline** — audible
  agent voices remain intact.
- **Painted-texture numbers on billiard balls** (`BilliardBallMaterial`
  + `NumberTexture` MID) — data on the ball surface, not floating text.
- **`AStation::DisplayName`** (`FString`) — used only in `UE_LOG`
  category strings, never rendered to screen.

## Acceptance Criteria

- **AC23.1** — `AStation::NameLabel` UPROPERTY removed (header + cpp
  construction). `BillboardLabel` method removed. `Tick` no longer
  references either.

- **AC23.2** — `AStation::TalkWidgetComponent` UPROPERTY +
  `TalkWidgetClass` UPROPERTY removed. `Speak / SpeakStreaming /
  ClearTalk / GetTalkWidget / WriteTalkText / TickStream / StreamTimer
  / StreamFullText / StreamCharIndex` removed. `SpeakAloud` reduced to
  the chat-subsystem TTS path only.

- **AC23.3** — `Source/AssemblyLineSimul/StationTalkWidget.h` and
  `Source/AssemblyLineSimul/StationTalkWidget.cpp` deleted.
  `Content/WBP_StationTalkPanel_Holo.uasset` deleted (orphan after
  the C++ class removal).

- **AC23.4** — `AAssemblyLineGameMode::StationTalkWidgetClass`
  UPROPERTY + its propagation block in `SpawnAssemblyLine` removed.
  Forward decl of `UStationTalkWidget` and `#include "StationTalkWidget.h"`
  removed from `AssemblyLineGameMode.h/cpp`.

- **AC23.5** — `ABucket::NumberBallLabels` UPROPERTY removed. Label
  construction in `RefreshContents` and the billboard-on-labels loop
  in `Tick` removed. Forward decl `class UTextRenderComponent;`
  dropped from `Bucket.h` if no other consumer.

- **AC23.6** — `AWorkerRobot::StateLabel` UPROPERTY removed plus any
  update / billboard call paths in `WorkerRobot.cpp`. Forward decl
  dropped from `WorkerRobot.h` if unused after.

- **AC23.7** — Tests updated:
  - `StationSpec.cpp` `Describe("TalkWidget", …)` block deleted (3 tests).
  - `StationSpec.cpp` Checker-verdict tests + SpeakAloud test KEPT
    (they assert on `Chat->LastSpokenForTesting`, which the TTS path
    still drives).
  - `BucketSpec.cpp` label-count assertions dropped from
    `RefreshContents` describe block.
  - `WorkerRobotSpec.cpp` `StateLabel` assertions dropped if any.
  - `AssemblyLineGameModeSpec.cpp` `propagates StationTalkWidgetClass`
    test deleted.

- **AC23.8** — All remaining tests pass after removal.

- **AC23.9** — `git grep -E 'NameLabel|NumberBallLabels|TalkWidgetComponent|UStationTalkWidget|StationTalkWidget|BillboardLabel|GetTalkWidget|StateLabel'`
  in `Source/` returns zero hits.

## Out of scope

- Removing `UAgentChatWidget` (the Tab HUD) — that's interactive input,
  not decorative text.
- Removing on-screen debug messages from voice pipeline — operational.
- Removing painted-texture numbers from billiard balls — data viz.
- Removing `AStation::DisplayName` — used by logs only.
