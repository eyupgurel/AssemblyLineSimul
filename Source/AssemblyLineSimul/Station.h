#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AssemblyLineTypes.h"
#include "Station.generated.h"

class ABucket;
class UAgentChatSubsystem;
class UPointLightComponent;
class UStaticMeshComponent;
class USceneComponent;
class UTextRenderComponent;
class UWidgetComponent;
class UStationTalkWidget;

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

	// Visible flat-top "workbench" mesh on top of the station; bucket sits here during Working.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<UStaticMeshComponent> WorkTable;

	// Attachment point on top of WorkTable — bucket is parented here during the Working phase.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<USceneComponent> BucketDockPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<UTextRenderComponent> NameLabel;

	// Point light that glows when this station is the voice subsystem's active agent.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<UPointLightComponent> ActiveLight;

	// In-world UMG panel surfacing the agent's current thoughts (e.g. checker's streaming LLM verdict).
	UPROPERTY(VisibleAnywhere, Category = "Station")
	TObjectPtr<UWidgetComponent> TalkWidgetComponent;

	// Widget class used to construct the talk panel. Override with a Blueprint subclass for styling.
	UPROPERTY(EditAnywhere, Category = "Station")
	TSubclassOf<UStationTalkWidget> TalkWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station")
	EStationType StationType = EStationType::Generator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ErrorRate = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station")
	FString DisplayName = TEXT("Station");

	// Plain-English rule that drives this station's ProcessBucket. Subclass constructors set
	// the default; UAgentChatSubsystem updates this when the user instructs the agent via chat.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station")
	FString CurrentRule;

	virtual void ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete);

	// Returns the rule the station should actually use right now. Defaults to CurrentRule;
	// Checker overrides to compose Generator/Filter/Sorter rules when in derived mode.
	virtual FString GetEffectiveRule() const { return CurrentRule; }

	// Called by the chat subsystem after writing a chat-supplied rule into CurrentRule.
	// Default no-op; Checker uses this to flip out of derived mode.
	virtual void OnRuleSetByChat() {}

	// Toggles the ActiveLight (used by VoiceSubsystem hookup).
	UFUNCTION(BlueprintCallable, Category = "Station")
	void SetActive(bool bActive);

	// Set the talk-panel text immediately.
	UFUNCTION(BlueprintCallable, Category = "Station")
	void Speak(const FString& Text);

	// Reveal the text character-by-character on the talk panel over CharsPerSecond.
	UFUNCTION(BlueprintCallable, Category = "Station")
	void SpeakStreaming(const FString& Text, float CharsPerSecond = 35.f);

	// Like SpeakStreaming but ALSO routes the text through the chat subsystem's
	// macOS-`say` pipeline so the audience hears it (used by the Checker for its
	// PASS/REJECT verdict, where the verdict path doesn't go through chat).
	UFUNCTION(BlueprintCallable, Category = "Station")
	void SpeakAloud(const FString& Text, float CharsPerSecond = 35.f);

	// Tests don't construct a real GameInstance, so they inject the chat
	// subsystem directly. SpeakAloud uses this override when present, otherwise
	// looks up via GetWorld()->GetGameInstance()->GetSubsystem<>().
	void SetChatSubsystemForTesting(UAgentChatSubsystem* Chat) { TestChatOverride = Chat; }

	UFUNCTION(BlueprintCallable, Category = "Station")
	void ClearTalk();

	// Returns the UMG widget instance hosted by TalkWidgetComponent, lazily creating it on first call.
	UStationTalkWidget* GetTalkWidget();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	FString StreamFullText;
	int32 StreamCharIndex = 0;
	FTimerHandle StreamTimer;
	void TickStream();
	void BillboardLabel(USceneComponent* Comp);
	void WriteTalkText(const FString& Text);

	UPROPERTY()
	TObjectPtr<UAgentChatSubsystem> TestChatOverride;
};
