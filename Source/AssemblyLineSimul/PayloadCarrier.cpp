#include "PayloadCarrier.h"
#include "Payload.h"
#include "PayloadVisualizer.h"
#include "Components/SceneComponent.h"

APayloadCarrier::APayloadCarrier()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// Sensible defaults so a carrier spawned with no Blueprint configuration
	// still works as the typical bucket-of-numbers + billiard balls combo.
	PayloadClass    = UIntegerArrayPayload::StaticClass();
	VisualizerClass = UBilliardBallVisualizer::StaticClass();
}

void APayloadCarrier::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Instantiate Payload from PayloadClass if missing or wrong type. A
	// designer-set PayloadClass on a Blueprint subclass is honored; runtime
	// code that needs to bypass the default can NewObject + assign Payload
	// directly before OnConstruction (e.g., Director->BucketClass spawning
	// pre-OnConstruction; or test helpers).
	if (PayloadClass && (!Payload || !Payload->IsA(PayloadClass)))
	{
		Payload = NewObject<UPayload>(this, PayloadClass);
	}

	if (VisualizerClass && (!Visualizer || !Visualizer->IsA(VisualizerClass)))
	{
		Visualizer = NewObject<UPayloadVisualizer>(this, VisualizerClass);
		if (Visualizer)
		{
			Visualizer->SetupAttachment(RootComponent);
			Visualizer->RegisterComponent();
		}
	}

	if (Visualizer && Payload)
	{
		Visualizer->BindPayload(Payload);
	}
}

FString APayloadCarrier::GetContentsString() const
{
	return Payload ? Payload->ToPromptString() : TEXT("[]");
}

void APayloadCarrier::HighlightItemsAtIndices(const TArray<int32>& Indices)
{
	if (Visualizer)
	{
		Visualizer->HighlightItemsAtIndices(Indices);
	}
}

APayloadCarrier* APayloadCarrier::CloneIntoWorld(UWorld* World, const FVector& Location) const
{
	if (!World) return nullptr;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APayloadCarrier* Clone = World->SpawnActor<APayloadCarrier>(
		GetClass(), Location, FRotator::ZeroRotator, Params);
	if (!Clone) return nullptr;

	// Deep-clone the payload data into the new carrier's outer. The clone's
	// OnConstruction already instantiated a fresh default Payload via
	// PayloadClass; overwrite it with our cloned data and re-bind.
	if (Payload)
	{
		Clone->Payload = Payload->Clone(Clone);
		if (Clone->Visualizer && Clone->Payload)
		{
			Clone->Visualizer->BindPayload(Clone->Payload);
		}
	}
	return Clone;
}
