#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PayloadCarrier.generated.h"

class UPayload;
class UPayloadVisualizer;

// Story 38 — the actor workers physically carry through the line, the
// camera follows, and the Director clones for fan-out / queues for fan-in.
// Replaces the pre-Story-38 ABucket. One concrete actor type for the entire
// system; pluggable Payload + Visualizer components decide what data flows
// and how it's rendered.
//
// Designer workflow:
//   - Create a Blueprint subclass (e.g. BP_NumberCarrier).
//   - Set PayloadClass    = UIntegerArrayPayload (or UTextPayload, etc.).
//   - Set VisualizerClass = UBilliardBallVisualizer (or UScrollVisualizer, etc.).
//   - Drop the Blueprint as the Director's CarrierClass.
//
// At OnConstruction the carrier instantiates both components, attaches the
// visualizer to the root, and binds the payload so the visualizer rebuilds
// on every payload mutation (Payload->OnChanged).
UCLASS(BlueprintType)
class ASSEMBLYLINESIMUL_API APayloadCarrier : public AActor
{
	GENERATED_BODY()

public:
	APayloadCarrier();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Carrier")
	TObjectPtr<USceneComponent> SceneRoot;

	// Designer-set: which UPayload subclass this carrier instantiates.
	// Default UIntegerArrayPayload preserves byte-identical behavior with
	// the pre-Story-38 ABucket for the typical 4-station mission.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier")
	TSubclassOf<UPayload> PayloadClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carrier")
	TSubclassOf<UPayloadVisualizer> VisualizerClass;

	// Instantiated at OnConstruction from PayloadClass. Stations cast to
	// the expected subclass at ProcessBucket entry.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Carrier")
	TObjectPtr<UPayload> Payload;

	// Instantiated + attached to RootComponent at OnConstruction from
	// VisualizerClass. Re-renders on every Payload->OnChanged broadcast.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Carrier")
	TObjectPtr<UPayloadVisualizer> Visualizer;

	// Convenience pass-through: Carrier->GetContentsString() equivalent to
	// Payload->ToPromptString() with safe nullability. Used by ChatSubsystem
	// to embed the current payload in Claude prompts.
	UFUNCTION(BlueprintCallable, Category = "Carrier")
	FString GetContentsString() const;

	// Convenience pass-through to Visualizer->HighlightItemsAtIndices.
	UFUNCTION(BlueprintCallable, Category = "Carrier")
	void HighlightItemsAtIndices(const TArray<int32>& Indices);

	// Story 31c — spawn an independent copy of this carrier at Location.
	// Spawns a new actor of the same Class (preserves Blueprint defaults
	// like PayloadClass/VisualizerClass), then deep-clones the Payload so
	// the clone has its own independent data. The Visualizer is fresh per
	// carrier — presentation is not part of the data.
	APayloadCarrier* CloneIntoWorld(UWorld* World, const FVector& Location) const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
};
