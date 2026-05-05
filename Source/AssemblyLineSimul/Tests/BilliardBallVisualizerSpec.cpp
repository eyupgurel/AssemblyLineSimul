#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Payload.h"
#include "PayloadCarrier.h"
#include "PayloadVisualizer.h"
#include "TestPayloads.h"

namespace AssemblyLineBilliardVisualizerTests
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

DEFINE_SPEC(FBilliardBallVisualizerSpec,
	"AssemblyLineSimul.BilliardBallVisualizer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FBilliardBallVisualizerSpec::Define()
{
	using namespace AssemblyLineBilliardVisualizerTests;

	Describe("Crate construction (carrier OnConstruction wires visualizer)", [this]()
	{
		It("an empty carrier has 12 wireframe crate edges", [this]()
		{
			FScopedTestWorld TW(TEXT("VisualizerSpec_CrateEdges"));
			APayloadCarrier* C = MakeNumberCarrier(TW.World, FVector::ZeroVector, {});
			if (!C) return;
			UBilliardBallVisualizer* V = Cast<UBilliardBallVisualizer>(C->Visualizer);
			TestNotNull(TEXT("BilliardBallVisualizer attached"), V);
			if (!V) return;
			TestEqual(TEXT("12 crate edges"), V->CrateEdges.Num(), 12);
		});
	});

	Describe("Rebuild on payload change", [this]()
	{
		It("after the payload broadcasts OnChanged, NumberBalls.Num matches Items.Num", [this]()
		{
			FScopedTestWorld TW(TEXT("VisualizerSpec_RebuildBalls"));
			APayloadCarrier* C = MakeNumberCarrier(TW.World, FVector::ZeroVector, {3, 7, 9, 11, 13});
			if (!C) return;
			UBilliardBallVisualizer* V = Cast<UBilliardBallVisualizer>(C->Visualizer);
			TestNotNull(TEXT("visualizer"), V);
			if (!V) return;
			TestEqual(TEXT("5 balls"), V->NumberBalls.Num(), 5);
		});

		It("clears the visualization (zero balls) when items become empty", [this]()
		{
			FScopedTestWorld TW(TEXT("VisualizerSpec_ClearOnEmpty"));
			APayloadCarrier* C = MakeNumberCarrier(TW.World, FVector::ZeroVector, {1, 2});
			if (!C) return;
			UBilliardBallVisualizer* V = Cast<UBilliardBallVisualizer>(C->Visualizer);
			if (!V) return;
			TestEqual(TEXT("2 balls before"), V->NumberBalls.Num(), 2);

			Cast<UIntegerArrayPayload>(C->Payload)->SetItems({});
			TestEqual(TEXT("0 balls after"), V->NumberBalls.Num(), 0);
		});

		It("rebuilds without leaking when called twice", [this]()
		{
			FScopedTestWorld TW(TEXT("VisualizerSpec_NoLeakOnRebuild"));
			APayloadCarrier* C = MakeNumberCarrier(TW.World, FVector::ZeroVector, {});
			if (!C) return;
			UIntegerArrayPayload* P = Cast<UIntegerArrayPayload>(C->Payload);
			UBilliardBallVisualizer* V = Cast<UBilliardBallVisualizer>(C->Visualizer);
			if (!P || !V) return;
			P->SetItems({1, 2, 3});
			TestEqual(TEXT("3 balls after first rebuild"), V->NumberBalls.Num(), 3);
			P->SetItems({4, 5});
			TestEqual(TEXT("2 balls after second rebuild (no leftover from first)"),
				V->NumberBalls.Num(), 2);
		});
	});

	Describe("OnVisualizationRevealed (Story 38 — replaces Bucket::OnContentsRevealed)", [this]()
	{
		It("fires once when items go from empty to non-empty", [this]()
		{
			FScopedTestWorld TW(TEXT("VisualizerSpec_OnRevealed"));
			APayloadCarrier* C = MakeNumberCarrier(TW.World, FVector::ZeroVector, {});
			if (!C) return;
			UBilliardBallVisualizer* V = Cast<UBilliardBallVisualizer>(C->Visualizer);
			UIntegerArrayPayload* P = Cast<UIntegerArrayPayload>(C->Payload);
			if (!V || !P) return;

			int32 Calls = 0;
			V->OnVisualizationRevealed.AddLambda([&]() { ++Calls; });

			P->SetItems({42});
			TestEqual(TEXT("OnVisualizationRevealed fired"), Calls, 1);
		});
	});

	Describe("HighlightItemsAtIndices", [this]()
	{
		It("empty Indices is a no-op", [this]()
		{
			FScopedTestWorld TW(TEXT("VisualizerSpec_HighlightEmpty"));
			APayloadCarrier* C = MakeNumberCarrier(TW.World, FVector::ZeroVector, {1, 2, 3});
			if (!C) return;
			C->HighlightItemsAtIndices({});
			TestTrue(TEXT("survived empty-indices call"), true);
		});

		It("only the named indices receive a different material instance", [this]()
		{
			FScopedTestWorld TW(TEXT("VisualizerSpec_HighlightSelected"));
			APayloadCarrier* C = MakeNumberCarrier(TW.World, FVector::ZeroVector, {10, 20, 30, 40});
			if (!C) return;
			UBilliardBallVisualizer* V = Cast<UBilliardBallVisualizer>(C->Visualizer);
			if (!V || V->NumberBalls.Num() != 4) return;

			UMaterialInterface* BeforeBall0 = V->NumberBalls[0]->GetMaterial(0);
			UMaterialInterface* BeforeBall1 = V->NumberBalls[1]->GetMaterial(0);

			C->HighlightItemsAtIndices({1, 3});  // highlight indices 1 and 3

			TestEqual(TEXT("ball 0 material unchanged"),
				V->NumberBalls[0]->GetMaterial(0), BeforeBall0);
			TestNotEqual(TEXT("ball 1 material changed (highlighted)"),
				V->NumberBalls[1]->GetMaterial(0), BeforeBall1);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
