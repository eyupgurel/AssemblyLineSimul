#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AssemblyLineTypes.h"
#include "Station.generated.h"

class ABucket;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Station")
	TObjectPtr<UTextRenderComponent> NameLabel;

	// In-world UMG panel surfacing the agent's current thoughts (e.g. checker's streaming LLM verdict).
	UPROPERTY(VisibleAnywhere, Category = "Station")
	TObjectPtr<UWidgetComponent> TalkWidgetComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station")
	EStationType StationType = EStationType::Generator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ErrorRate = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station")
	FString DisplayName = TEXT("Station");

	virtual void ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete);

	// Set the talk-panel text immediately.
	UFUNCTION(BlueprintCallable, Category = "Station")
	void Speak(const FString& Text);

	// Reveal the text character-by-character on the talk panel over CharsPerSecond.
	UFUNCTION(BlueprintCallable, Category = "Station")
	void SpeakStreaming(const FString& Text, float CharsPerSecond = 35.f);

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
};
