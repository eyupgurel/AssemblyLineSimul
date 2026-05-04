#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AssemblyLineTypes.h"
#include "DAG/AssemblyLineDAG.h"  // FNodeRef on AStation (Story 35)
#include "Station.generated.h"

class ABucket;
class UAgentChatSubsystem;
class UPointLightComponent;
class UStaticMeshComponent;
class USceneComponent;

UCLASS(Abstract)
class ASSEMBLYLINESIMUL_API AStation : public AActor
{
	GENERATED_BODY()

public:
	AStation();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<USceneComponent> InputSlot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<USceneComponent> OutputSlot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<USceneComponent> WorkerStandPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<UStaticMeshComponent> WorkTable;

	// Attachment point on top of WorkTable — bucket is parented here during the Working phase.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<USceneComponent> BucketDockPoint;

	// Point light that glows when this station is the voice subsystem's active agent.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<UPointLightComponent> ActiveLight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station")
	EStationType StationType = EStationType::Generator;

	// Story 35 — multi-instance per Kind. Set by Director::RegisterStation
	// (auto-instance counter). Defaults to {Generator, 0}; the real value
	// is back-written on registration so dispatch can identify which
	// instance just finished.
	FNodeRef NodeRef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ErrorRate = 0.0f;

	// Used in UE_LOG category strings only — never rendered in-world.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station")
	FString DisplayName = TEXT("Station");

	// Plain-English rule that drives this station's ProcessBucket. Subclass constructors set
	// the default; UAgentChatSubsystem updates this when the user instructs the agent via chat.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station")
	FString CurrentRule;

	// Story 31b — accepts multiple input buckets to support fan-in (Story 31d).
	// Today (and through 31c) Inputs.Num() is always 1; subclasses read Inputs[0].
	virtual void ProcessBucket(const TArray<ABucket*>& Inputs, FStationProcessComplete OnComplete);

	// Returns the rule the station should actually use right now. Defaults to CurrentRule;
	// Checker overrides to compose Generator/Filter/Sorter rules when in derived mode.
	virtual FString GetEffectiveRule() const { return CurrentRule; }

	// Called by the chat subsystem after writing a chat-supplied rule into CurrentRule.
	// Default no-op; Checker uses this to flip out of derived mode.
	virtual void OnRuleSetByChat() {}

	// Toggles the ActiveLight (used by VoiceSubsystem hookup).
	UFUNCTION(BlueprintCallable, Category = "Station")
	void SetActive(bool bActive);

	// Routes the text through the chat subsystem's macOS-`say` TTS pipeline so
	// the audience HEARS it (Checker verdicts, recycle announcement, etc.).
	// In-world panel is gone — this is the sole audible-output path.
	UFUNCTION(BlueprintCallable, Category = "Station")
	void SpeakAloud(const FString& Text);

	// Tests don't construct a real GameInstance, so they inject the chat
	// subsystem directly. SpeakAloud uses this override when present, otherwise
	// looks up via GetWorld()->GetGameInstance()->GetSubsystem<>().
	void SetChatSubsystemForTesting(UAgentChatSubsystem* Chat) { TestChatOverride = Chat; }

private:
	UPROPERTY()
	TObjectPtr<UAgentChatSubsystem> TestChatOverride;
};
