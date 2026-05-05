#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Payload.h"
#include "PayloadCarrier.h"

// Story 38 — test ergonomics helper. Pre-Story-38 tests did:
//   ABucket* B = SpawnActor<ABucket>(...); B->Contents = {1,2,3};
// With the payload-carrier abstraction, the equivalent is:
//   APayloadCarrier* C = MakeNumberCarrier(World, Loc, {1,2,3});
//
// Spawns a default APayloadCarrier (PayloadClass = UIntegerArrayPayload,
// VisualizerClass = UBilliardBallVisualizer per the carrier's defaults),
// then casts the auto-instantiated payload and writes Items + broadcasts
// OnChanged so the visualizer rebuilds.
inline APayloadCarrier* MakeNumberCarrier(UWorld* World, const FVector& Loc, const TArray<int32>& Items)
{
	if (!World) return nullptr;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APayloadCarrier* Carrier = World->SpawnActor<APayloadCarrier>(
		APayloadCarrier::StaticClass(), Loc, FRotator::ZeroRotator, Params);
	if (!Carrier) return nullptr;
	if (UIntegerArrayPayload* P = Cast<UIntegerArrayPayload>(Carrier->Payload))
	{
		P->Items = Items;
		P->OnChanged.Broadcast();
	}
	return Carrier;
}
