# Story 19 — Active-agent green glow on the worker

Approved 2026-04-27.

## Goal

When the operator's voice command targets a specific agent, the worker robot
itself glows green — same hue as the `AAssemblyLineFeedback` "accept" flash —
so the audience instantly sees who the operator is addressing. The current
cyan glow on the *station* (Story 14) reads too much like ambient lighting
and competes with the worker for attention.

## Acceptance Criteria

- **AC19.1** — `AWorkerRobot` has a `UPointLightComponent` (green,
  ~600 cm attenuation radius) attached above the worker, **off by default**
  (`Intensity = 0`).

- **AC19.2** — `AWorkerRobot::SetActive(bool bActive)` toggles the worker
  light's intensity (e.g. `8000` when on, `0` when off).

- **AC19.3** — `AAssemblyLineGameMode::HandleActiveAgentChanged` calls
  `SetActive(true)` on the worker for the new active agent and
  `SetActive(false)` on every other worker. Stations are no longer touched
  from this handler.

- **AC19.4** — `AStation::SetActive` is left in place as a no-op-ish legacy
  hook (still BP-callable) but is no longer invoked from
  `HandleActiveAgentChanged`. Future story can remove it if it stays
  unused.

- **AC19.5** — Tests:
  - Freshly spawned `AWorkerRobot` has its `ActiveLight` component present
    with `Intensity == 0` and a green `LightColor`.
  - `SetActive(true)` makes the intensity > 0; `SetActive(false)` returns
    it to 0.

## Out of scope

- Animating the glow (pulse / fade-in / fade-out). Hard on/off matches the
  existing accept/reject feedback style.
- Removing `AStation::SetActive` entirely.
- Multi-agent active state (only one agent is active at a time per
  `UVoiceSubsystem`).
