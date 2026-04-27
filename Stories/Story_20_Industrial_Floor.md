# Story 20 — Industrial floor under the assembly line (asset-driven)

Approved 2026-04-27.

## Goal

Replace the void under the assembly line with a real textured industrial
floor. Uses the imported **Stylized Metallic Floor** asset pack at
`Content/Metallic_Floor/` (SM_Metallic_Floor static mesh + M_Metallic_Floor
material + textures). The earlier procedural-cube approach was abandoned
after Substrate-driven materials refused to honor the runtime tint we set
on engine `BasicShapeMaterial`-based primitives.

The source mesh is a 200×200 cm flat plane whose material tiles via UV at
its native size. To cover the line without stretching the texture, the
floor is laid down as a grid of mesh-instance tiles at native scale.

## Acceptance Criteria

- **AC20.1** — `AAssemblyLineGameMode` gains:
  - `FloorMesh` (`TSoftObjectPtr<UStaticMesh>`, EditAnywhere, default unset)
  - `FloorScale` (`FVector`, EditAnywhere, default `(1, 1, 1)` — per-tile scale)
  - `FloorTilesX` / `FloorTilesY` (`int32`, EditAnywhere, defaults `60` / `60`)
  - `FloorOffset` (`FVector`, EditAnywhere, default `(0, 0, 85)` — added to the
    grid center; default Z lifts tiles flush with station cube bottoms)

- **AC20.2** — `SpawnFloor()` is called from `BeginPlay` after
  `SpawnAssemblyLine()`. It spawns `FloorTilesX × FloorTilesY`
  `AStaticMeshActor` tiles, each at native mesh scale × `FloorScale`, laid
  out as a grid centered on the line midpoint + `FloorOffset.XY`. **If
  `FloorMesh` is unset, nothing is spawned** (so test environments and
  projects without the asset pack stay unaffected).

- **AC20.3** — Each tile sits at
  `Z = LineOrigin.Z - 135 + FloorOffset.Z`
  i.e. `Z = 50` with default `LineOrigin.Z = 100` and `FloorOffset.Z = 85`,
  putting the tiles flush with the station cube bottoms. Every tile is
  tagged `AssemblyLineFloor` so specs can find them.

- **AC20.4** — `BP_AssemblyLineGameMode` (editor-side, no code change) has
  `FloorMesh` assigned to
  `/Game/Metallic_Floor/StaticMesh/SM_Metallic_Floor`.

- **AC20.5** — Tests on `AAssemblyLineGameMode`:
  - Calling `SpawnFloor()` with `FloorMesh` unset → 0 actors tagged
    `AssemblyLineFloor`.
  - Calling `SpawnFloor()` with `FloorMesh` set to the engine cube and
    `FloorTilesX = 4`, `FloorTilesY = 3` → exactly 12 actors tagged
    `AssemblyLineFloor`, each carrying that mesh and with
    `GetActorScale3D() == FloorScale`.

- **AC20.6** — Manual: with default `FloorTilesX = 60`, `FloorTilesY = 60`
  and the default cinematic shot set (1 wide overview + 4 station
  closeups), the floor extends past every shot's view frustum so no void
  is visible from any camera angle. If new wider shots are added to the
  cinematic, bump `FloorTilesX/Y` until coverage holds.

## Out of scope

- Lane markers (parked — the floor texture itself reads as industrial; no
  need for separate yellow tape).
- Wall props, ceiling, lighting fixtures.
- Conveyor belt between stations (Story 21 candidate).
- Tiling parameters on the material — the asset already tiles via UV.
