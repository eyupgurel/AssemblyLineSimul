#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Payload.h"
#include "PayloadCarrier.h"
#include "PayloadVisualizer.h"
#include "TestPayloads.h"

namespace AssemblyLinePayloadCarrierTests
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld(const TCHAR* Name)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				World->BeginTearingDown();
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

DEFINE_SPEC(FPayloadCarrierSpec,
	"AssemblyLineSimul.PayloadCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FPayloadCarrierSpec::Define()
{
	using namespace AssemblyLinePayloadCarrierTests;

	Describe("OnConstruction", [this]()
	{
		It("instantiates Payload + Visualizer from default class properties", [this]()
		{
			FScopedTestWorld TW(TEXT("CarrierSpec_OnConstruction"));
			APayloadCarrier* C = MakeNumberCarrier(TW.World, FVector::ZeroVector, {});
			TestNotNull(TEXT("carrier spawned"), C);
			if (!C) return;

			TestNotNull(TEXT("Payload auto-instantiated"), C->Payload.Get());
			TestNotNull(TEXT("Visualizer auto-instantiated"), C->Visualizer.Get());

			TestTrue(TEXT("default Payload is UIntegerArrayPayload"),
				C->Payload->IsA(UIntegerArrayPayload::StaticClass()));
			TestTrue(TEXT("default Visualizer is UBilliardBallVisualizer"),
				C->Visualizer->IsA(UBilliardBallVisualizer::StaticClass()));
		});

		It("Visualizer auto-binds to Payload (Visualizer.GetBoundPayload == Payload)", [this]()
		{
			FScopedTestWorld TW(TEXT("CarrierSpec_AutoBind"));
			APayloadCarrier* C = MakeNumberCarrier(TW.World, FVector::ZeroVector, {});
			if (!C || !C->Visualizer) return;
			TestEqual(TEXT("Visualizer bound to Carrier's Payload"),
				C->Visualizer->GetBoundPayload(), C->Payload.Get());
		});
	});

	Describe("Pass-throughs", [this]()
	{
		It("GetContentsString delegates to Payload->ToPromptString", [this]()
		{
			FScopedTestWorld TW(TEXT("CarrierSpec_GetContentsString"));
			APayloadCarrier* C = MakeNumberCarrier(TW.World, FVector::ZeroVector, {1, 2, 3});
			if (!C) return;
			TestEqual(TEXT("[1, 2, 3]"),
				C->GetContentsString(), FString(TEXT("[1, 2, 3]")));
		});

		It("GetContentsString returns '[]' when Payload is null", [this]()
		{
			FScopedTestWorld TW(TEXT("CarrierSpec_GetContentsString_Null"));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APayloadCarrier* C = TW.World->SpawnActor<APayloadCarrier>(
				APayloadCarrier::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!C) return;
			C->Payload = nullptr;
			TestEqual(TEXT("'[]' when Payload null"),
				C->GetContentsString(), FString(TEXT("[]")));
		});
	});

	Describe("CloneIntoWorld (Story 31c — fan-out)", [this]()
	{
		It("returns a distinct actor with deep-cloned Payload (mutating clone's items doesn't affect source)", [this]()
		{
			FScopedTestWorld TW(TEXT("CarrierSpec_Clone_Independent"));
			APayloadCarrier* Src = MakeNumberCarrier(TW.World, FVector::ZeroVector, {7, 11, 13});
			if (!Src) return;

			APayloadCarrier* Clone = Src->CloneIntoWorld(TW.World, FVector(100.f, 0.f, 0.f));
			TestNotNull(TEXT("Clone spawned"), Clone);
			if (!Clone) return;

			TestNotEqual(TEXT("Clone is a distinct actor"), Clone, Src);

			UIntegerArrayPayload* SrcP   = Cast<UIntegerArrayPayload>(Src->Payload);
			UIntegerArrayPayload* CloneP = Cast<UIntegerArrayPayload>(Clone->Payload);
			TestNotNull(TEXT("source payload still UIntegerArrayPayload"), SrcP);
			TestNotNull(TEXT("clone payload UIntegerArrayPayload"), CloneP);
			if (!SrcP || !CloneP) return;

			TestEqual(TEXT("Clone has same Items as source"), CloneP->Items, SrcP->Items);

			CloneP->Items.Add(99);
			TestEqual(TEXT("source unchanged after clone mutation"),
				SrcP->Items.Num(), 3);
		});

		It("Clone's Visualizer is a distinct instance bound to the cloned Payload", [this]()
		{
			FScopedTestWorld TW(TEXT("CarrierSpec_Clone_VisualizerFresh"));
			APayloadCarrier* Src = MakeNumberCarrier(TW.World, FVector::ZeroVector, {1});
			APayloadCarrier* Clone = Src ? Src->CloneIntoWorld(TW.World, FVector(100.f, 0.f, 0.f)) : nullptr;
			if (!Src || !Clone) return;
			TestNotEqual(TEXT("Visualizers are distinct objects"),
				(UPayloadVisualizer*)Clone->Visualizer.Get(), (UPayloadVisualizer*)Src->Visualizer.Get());
			TestEqual(TEXT("Clone visualizer bound to clone payload"),
				Clone->Visualizer->GetBoundPayload(), Clone->Payload.Get());
		});
	});

	Describe("HighlightItemsAtIndices delegates to Visualizer", [this]()
	{
		It("does not crash when called with empty Indices", [this]()
		{
			FScopedTestWorld TW(TEXT("CarrierSpec_Highlight_Empty"));
			APayloadCarrier* C = MakeNumberCarrier(TW.World, FVector::ZeroVector, {1, 2, 3});
			if (!C) return;
			C->HighlightItemsAtIndices({});  // no-op contract on visualizer
			TestTrue(TEXT("survived empty-indices call"), true);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
