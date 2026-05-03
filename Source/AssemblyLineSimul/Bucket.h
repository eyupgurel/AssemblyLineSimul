#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Bucket.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UFont;

DECLARE_MULTICAST_DELEGATE(FOnBucketContentsRevealed);

UCLASS()
class ASSEMBLYLINESIMUL_API ABucket : public AActor
{
	GENERATED_BODY()

public:
	ABucket();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bucket")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bucket")
	TArray<int32> Contents;

	// 12 thin cylinders forming the see-through crate frame.
	UPROPERTY(VisibleAnywhere, Category = "Bucket")
	TArray<TObjectPtr<UStaticMeshComponent>> CrateEdges;

	// One sphere per Contents entry, rebuilt by RefreshContents.
	UPROPERTY(VisibleAnywhere, Category = "Bucket")
	TArray<TObjectPtr<UStaticMeshComponent>> NumberBalls;

	// When set, RefreshContents paints each ball with a render-target texture of its number
	// applied via a dynamic material instance of this material. Expected parameters on the
	// material: NumberTexture (Texture2D), BaseColor (Vector).
	UPROPERTY(EditAnywhere, Category = "Bucket")
	TSoftObjectPtr<UMaterialInterface> BilliardBallMaterial;

	// Rotation applied to each ball so its painted number faces up. UE's basic sphere UV
	// puts (U=0.5, V=0.5) at one specific longitude on the equator; this rotation orients
	// that point toward +Z. Adjust in a Blueprint subclass if the default isn't right.
	UPROPERTY(EditAnywhere, Category = "Bucket")
	FRotator BallRelativeRotation = FRotator(-90.f, 0.f, 0.f);

	// Color driven into EmissiveMeshMaterial's "Color" parameter on each crate
	// edge — RGB > 1 triggers HDR bloom so the wireframe reads as glowing gold.
	UPROPERTY(EditAnywhere, Category = "Bucket")
	FLinearColor GlassTint = FLinearColor(2.5f, 1.8f, 0.3f, 1.f);

	// Fires when RefreshContents transitions Contents from empty to non-empty — the
	// cinematic uses this to defer zoom-in for the Generator until balls actually appear.
	FOnBucketContentsRevealed OnContentsRevealed;

	UFUNCTION(BlueprintCallable, Category = "Bucket")
	FString GetContentsString() const;

	// Rebuilds NumberBalls to match Contents.
	UFUNCTION(BlueprintCallable, Category = "Bucket")
	void RefreshContents();

	// Story 25 — paints only the named ball indices with EmissiveMeshMaterial
	// tinted to GlassTint. Other balls keep their normal material. Used by
	// AFilterStation to flag the kept subset for 1 s before dropping the rest.
	UFUNCTION(BlueprintCallable, Category = "Bucket")
	void HighlightBallsAtIndices(const TArray<int32>& Indices);

	// Story 31c — spawn an independent copy of this bucket at Location.
	// Copies Contents and BilliardBallMaterial, then runs RefreshContents on
	// the new actor so its visualization is fully built. Used by the Director
	// when a DAG node has K > 1 successors (one clone per branch).
	ABucket* CloneIntoWorld(UWorld* World, const FVector& Location) const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	// Cached at construction so RefreshContents can build balls without runtime asset lookup.
	UPROPERTY()
	TObjectPtr<UStaticMesh> CachedSphereMesh;

	UPROPERTY()
	TObjectPtr<UFont> CachedNumberFont;
};
