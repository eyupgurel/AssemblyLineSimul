#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Bucket.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;

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

	UPROPERTY(VisibleAnywhere, Category = "Bucket")
	TArray<TObjectPtr<UTextRenderComponent>> NumberBallLabels;

	UFUNCTION(BlueprintCallable, Category = "Bucket")
	FString GetContentsString() const;

	// Rebuilds NumberBalls + NumberBallLabels to match Contents.
	UFUNCTION(BlueprintCallable, Category = "Bucket")
	void RefreshContents();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	// Cached at construction so RefreshContents can build balls without runtime asset lookup.
	UPROPERTY()
	TObjectPtr<UStaticMesh> CachedSphereMesh;
};
