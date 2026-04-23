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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bucket")
	TObjectPtr<UTextRenderComponent> ContentsLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bucket")
	TArray<int32> Contents;

	UFUNCTION(BlueprintCallable, Category = "Bucket")
	FString GetContentsString() const;

	UFUNCTION(BlueprintCallable, Category = "Bucket")
	void RefreshLabel();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
};
