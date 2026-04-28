# Story 22 — Cleanup pass after the gold-bucket pivot

Drafted 2026-04-28.

## Goal

Remove dead code orphaned during the abandoned Story 21 (fantasy
work-table import) and the subsequent pivot to a glowing-gold
wireframe bucket. The codebase should reflect the final state:
hidden station shells (no body cube, no tabletop), and a bucket
rendered as 12 emissive-gold wireframe edges with no inner mesh.

## Acceptance Criteria

- **AC22.1** — `ABucket::GlassMaterial` UPROPERTY removed from `Bucket.h`.
  After the EmissiveMeshMaterial switch, this property is no longer
  read anywhere — `Bucket.cpp::OnConstruction` loads
  `/Engine/EngineMaterials/EmissiveMeshMaterial` directly.

- **AC22.2** — In `AStation` constructor, drop the cube mesh assignments
  + ad-hoc scales that no longer have any visual effect (both
  `MeshComponent` and `WorkTable` are hidden):
  - `MeshComponent`: drop `SetStaticMesh` + `SetWorldScale3D` (the
    component has no children, so nothing depends on its size).
  - `WorkTable`: drop `SetStaticMesh`. **Keep** `SetRelativeLocation`
    and `SetRelativeScale3D` — those still position `BucketDockPoint`
    (a child of `WorkTable`) at the bucket's docking world Z.
  - Drop the `CubeFinder` `ConstructorHelpers::FObjectFinder` since
    neither path uses it anymore.

- **AC22.3** — Repo grep confirms no Story 21 / fantasy-table /
  WorkTable-asset remnants:
  ```
  git grep -E 'Story_21|fantasy|WorkTableMeshAsset|FloorAutoBounds|ApplyWorkTableMesh|Free_Fantasy' Source/
  ```
  returns zero hits.

- **AC22.4** — All 67/67 existing specs still pass after the cleanup.

- **AC22.5** — No diagnostic / debug `UE_LOG` lines remain from the
  table-iteration session. (Survey already done — only operational
  logs with categorized loggers exist; no action needed beyond
  recording this in the story.)

## Out of scope

- Renaming `ABucket::GlassTint` to a more accurate `BucketGlowColor`
  — would force a Blueprint property migration with no functional
  benefit. Comment-only update if needed.
- The `L_AssemblyDemo.umap` Bin-equal touch from prior sessions —
  unrelated to this story.
- A project-wide audit of older operational logs — those are
  intentional and out of scope for this cleanup.
