#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PayloadVisualizer.generated.h"

class UPayload;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UFont;

// Story 38 — fired AFTER Rebuild transitions the visualization from
// empty/hidden to populated/visible. Replaces ABucket::OnContentsRevealed.
// The cinematic camera subscribes to this on the Generator's bound carrier
// to defer zoom-in until the first batch of items actually appears.
DECLARE_MULTICAST_DELEGATE(FOnVisualizationRevealed);

// Story 38 — abstract SceneComponent that renders a bound UPayload as
// scene primitives. Subclasses own their geometry (mesh components,
// materials, etc.) and read typed data from the payload via Cast.
//
// Plug-and-play contract: each payload type has at least one corresponding
// visualizer (UIntegerArrayPayload ↔ UBilliardBallVisualizer is the v1
// pair). New payload types ship with their own visualizer; the carrier
// actor + runtime stay unchanged.
UCLASS(Abstract, BlueprintType, EditInlineNew)
class ASSEMBLYLINESIMUL_API UPayloadVisualizer : public USceneComponent
{
	GENERATED_BODY()

public:
	// Subscribes to Payload->OnChanged so the visualizer auto-Rebuilds
	// after any mutation. Called once by APayloadCarrier::OnConstruction
	// after both Payload + Visualizer are instantiated.
	UFUNCTION(BlueprintCallable, Category = "Visualizer")
	virtual void BindPayload(UPayload* InPayload);

	// Re-render scene primitives from current payload state. Subclasses
	// destroy + rebuild their mesh components here.
	UFUNCTION(BlueprintCallable, Category = "Visualizer")
	virtual void Rebuild() PURE_VIRTUAL(UPayloadVisualizer::Rebuild, );

	// Story 25 — flag a subset for the user's attention without removing
	// the un-flagged items. Filter calls this with the kept-indices to
	// show the audience which items survived its rule for ~1 s before
	// the rejected items vanish.
	//
	// Semantic contract: visualizer decides HOW to highlight. Billiard
	// balls glow gold; future scroll lines underline; future canvases
	// get a green border. Empty Indices is a no-op.
	UFUNCTION(BlueprintCallable, Category = "Visualizer")
	virtual void HighlightItemsAtIndices(const TArray<int32>& Indices) PURE_VIRTUAL(UPayloadVisualizer::HighlightItemsAtIndices, );

	UFUNCTION(BlueprintCallable, Category = "Visualizer")
	virtual void ClearHighlight() PURE_VIRTUAL(UPayloadVisualizer::ClearHighlight, );

	FOnVisualizationRevealed OnVisualizationRevealed;

	// Public accessor so tests + cinematic can introspect the bound payload.
	UPayload* GetBoundPayload() const { return BoundPayload; }

protected:
	UPROPERTY()
	TObjectPtr<UPayload> BoundPayload;

	// Default subscriber: on payload change, call Rebuild. Subclass override
	// is rarely needed; if needed, override BindPayload entirely.
	virtual void OnBoundPayloadChanged();
};

// Story 38 — concrete visualizer for UIntegerArrayPayload. Renders the
// 12-edge wireframe crate + per-item billiard sphere with a runtime-rendered
// number texture. Lifts the rendering code from the pre-Story-38
// ABucket::OnConstruction + RefreshContents + HighlightBallsAtIndices
// verbatim into a SceneComponent.
UCLASS(BlueprintType)
class ASSEMBLYLINESIMUL_API UBilliardBallVisualizer : public UPayloadVisualizer
{
	GENERATED_BODY()

public:
	UBilliardBallVisualizer();

	// 12 thin cylinders forming the see-through crate frame. Built once
	// at OnRegister; never rebuilt.
	UPROPERTY(VisibleAnywhere, Category = "Bucket")
	TArray<TObjectPtr<UStaticMeshComponent>> CrateEdges;

	// One sphere per payload entry, rebuilt by Rebuild().
	UPROPERTY(VisibleAnywhere, Category = "Bucket")
	TArray<TObjectPtr<UStaticMeshComponent>> NumberBalls;

	// Master material used to paint each ball with a runtime-rendered
	// number texture. Expected parameters: NumberTexture (Texture2D),
	// BaseColor (Vector). Pre-Story-38 this was on ABucket directly; now
	// owned by the visualizer.
	UPROPERTY(EditAnywhere, Category = "Bucket")
	TSoftObjectPtr<UMaterialInterface> BilliardBallMaterial;

	// Rotation applied to each ball so its painted number faces up. UE's
	// basic sphere UV puts (U=0.5, V=0.5) at one specific longitude on
	// the equator; this rotation orients that point toward +Z.
	UPROPERTY(EditAnywhere, Category = "Bucket")
	FRotator BallRelativeRotation = FRotator(90.f, 0.f, 0.f);

	// Color driven into EmissiveMeshMaterial's "Color" parameter on each
	// crate edge — RGB > 1 triggers HDR bloom so the wireframe reads as
	// glowing gold. Also used by HighlightItemsAtIndices to paint kept balls.
	UPROPERTY(EditAnywhere, Category = "Bucket")
	FLinearColor GlassTint = FLinearColor(2.5f, 1.8f, 0.3f, 1.f);

	virtual void Rebuild() override;
	virtual void HighlightItemsAtIndices(const TArray<int32>& Indices) override;
	virtual void ClearHighlight() override;

protected:
	virtual void OnRegister() override;

private:
	UPROPERTY()
	TObjectPtr<UStaticMesh> CachedSphereMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CachedCylinderMesh;

	UPROPERTY()
	TObjectPtr<UFont> CachedNumberFont;

	bool bCrateBuilt = false;

	void EnsureCrateBuilt();
};
