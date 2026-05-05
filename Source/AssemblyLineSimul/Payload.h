#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Payload.generated.h"

// Story 38 — pure-data UObject (no engine deps beyond UObject + CoreMinimal)
// representing a typed payload carried by APayloadCarrier through the assembly
// line. Stations cast to the expected subclass at ProcessBucket entry to read
// + mutate the typed data, then call OnChanged.Broadcast() so the bound
// visualizer rebuilds.
//
// New agent kinds (text agents, image agents, etc.) add a new UPayload
// subclass (UTextPayload, UImageRefPayload, ...) without touching any
// existing runtime — Director/Worker/Cinematic only see UPayload through
// APayloadCarrier.
DECLARE_MULTICAST_DELEGATE(FOnPayloadChanged);

UCLASS(Abstract, BlueprintType, EditInlineNew)
class ASSEMBLYLINESIMUL_API UPayload : public UObject
{
	GENERATED_BODY()

public:
	// Number of items currently held. Stations use this for empty-bucket
	// recycle detection (Director's RECYCLE branch).
	UFUNCTION(BlueprintCallable, Category = "Payload")
	virtual int32 ItemCount() const PURE_VIRTUAL(UPayload::ItemCount, return 0;);

	UFUNCTION(BlueprintCallable, Category = "Payload")
	virtual bool IsEmpty() const { return ItemCount() == 0; }

	// Plain-text rendering used when the payload is embedded in a Claude
	// prompt (e.g., "INPUT: [1, 2, 3]" in the Filter station's prompt template).
	// Subclass decides format — UIntegerArrayPayload returns "[1, 2, 3]".
	UFUNCTION(BlueprintCallable, Category = "Payload")
	virtual FString ToPromptString() const PURE_VIRTUAL(UPayload::ToPromptString, return FString(););

	// Deep clone owned by Outer. Used by APayloadCarrier::CloneIntoWorld for
	// fan-out (Story 31c — one independent clone per branch).
	virtual UPayload* Clone(UObject* Outer) const PURE_VIRTUAL(UPayload::Clone, return nullptr;);

	// Fired AFTER any mutation by the holding station. Visualizer subscribes
	// in BindPayload to re-render scene primitives. Cinematic also subscribes
	// (on the carrier's bound payload) to defer Generator zoom-in until the
	// first non-empty contents reveal.
	FOnPayloadChanged OnChanged;
};

// The concrete payload behind today's bucket-of-numbers: a TArray<int32>.
// Default for the typical Generator → Filter → Sorter → Checker mission
// chain. Pre-Story-38 this was hardcoded as ABucket::Contents.
UCLASS(BlueprintType)
class ASSEMBLYLINESIMUL_API UIntegerArrayPayload : public UPayload
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Payload")
	TArray<int32> Items;

	virtual int32 ItemCount() const override { return Items.Num(); }

	virtual FString ToPromptString() const override;

	virtual UPayload* Clone(UObject* Outer) const override;

	// Convenience setter that mutates and broadcasts in one call. Stations
	// can also write Items directly + call OnChanged.Broadcast() if they
	// need finer control.
	UFUNCTION(BlueprintCallable, Category = "Payload")
	void SetItems(const TArray<int32>& NewItems)
	{
		Items = NewItems;
		OnChanged.Broadcast();
	}
};
