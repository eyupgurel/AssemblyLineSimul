# Story 24 — Glow filter-selected balls in gold

Drafted 2026-04-28.

## Goal

After the Filter station applies its rule and narrows the bucket to
a subset, paint the surviving balls with the same emissive-gold
material as the bucket wireframe. Visually conveys "these are the
ones the Filter selected" while the bucket sits on the Filter dock,
without needing any in-world text.

## Acceptance Criteria

- **AC24.1** — `ABucket` gains a public method
  `ApplyGoldEmissiveToBalls()` (BlueprintCallable). For each entry in
  `NumberBalls`, it sets material slot 0 to a `UMaterialInstanceDynamic`
  of `/Engine/EngineMaterials/EmissiveMeshMaterial` with the `Color`
  parameter set to `GlassTint` — same gold the bucket wireframe uses,
  so the highlight reads as "selected by the same glow family."

- **AC24.2** — `AFilterStation`'s LLM response handler calls
  `B->ApplyGoldEmissiveToBalls()` immediately after `B->RefreshContents()`,
  so the freshly-built kept-subset balls render gold.

- **AC24.3** — No other station calls `ApplyGoldEmissiveToBalls`.
  Generator / Sorter / Checker bucket balls keep the painted-number
  `BilliardBallMaterial` path — only Filter-station balls glow.

- **AC24.4** — Test on `ABucket`: after `RefreshContents` for
  `Contents = {3, 5, 7}` followed by `ApplyGoldEmissiveToBalls()`,
  each ball's material slot 0 is a `UMaterialInstanceDynamic` whose
  Parent is the engine `EmissiveMeshMaterial`.

- **AC24.5** — All 63 existing specs still pass after the addition.

## Trade-off

- The painted-on number textures (from `BilliardBallMaterial`) are
  lost on Filter-table balls — replaced by solid emissive gold. The
  audience already saw the input numbers when the bucket arrived
  with all candidates, so the kept-subset's identities are inferable.
  Other stations' balls keep their painted numbers.

- Auto-cleared at the next station: Sorter's `RefreshContents`
  rebuilds balls without re-applying the highlight.

## Out of scope

- A combined material that shows both painted numbers AND emissive
  gold (would need a custom material asset with both `NumberTexture`
  and emissive `Color` parameters).
- Glowing only the balls that survived (vs all current balls) — after
  Filter's `RefreshContents`, the bucket only contains survivors, so
  "glow all current balls" === "glow selected ones."
- Glowing balls on Generator / Sorter / Checker tables.
