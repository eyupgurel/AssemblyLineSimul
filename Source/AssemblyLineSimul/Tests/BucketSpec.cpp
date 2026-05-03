#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Bucket.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace AssemblyLineBucketTests
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

	static ABucket* SpawnBucket(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ABucket>(
			ABucket::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
}

DEFINE_SPEC(FBucketSpec,
	"AssemblyLineSimul.Bucket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FBucketSpec::Define()
{
	using namespace AssemblyLineBucketTests;

	Describe("Construction", [this]()
	{
		It("hides the inner cube and shows the 12 emissive-gold wireframe edges", [this]()
		{
			FScopedTestWorld TW(TEXT("BucketSpec_Construct"));
			ABucket* Bucket = SpawnBucket(TW.World);
			TestNotNull(TEXT("bucket spawned"), Bucket);
			if (!Bucket) return;

			TestFalse(TEXT("inner cube hidden"), Bucket->MeshComponent->IsVisible());
			TestEqual(TEXT("12 crate edges constructed"), Bucket->CrateEdges.Num(), 12);
			for (UStaticMeshComponent* Edge : Bucket->CrateEdges)
			{
				if (Edge)
				{
					TestTrue(TEXT("each crate edge visible"), Edge->IsVisible());
				}
			}
		});
	});

	Describe("RefreshContents", [this]()
	{
		It("creates one number ball per Contents entry", [this]()
		{
			FScopedTestWorld TW(TEXT("BucketSpec_Refresh"));
			ABucket* Bucket = SpawnBucket(TW.World);
			if (!Bucket) return;

			Bucket->Contents = { 3, 5, 7 };
			Bucket->RefreshContents();

			TestEqual(TEXT("ball count"), Bucket->NumberBalls.Num(), 3);
		});

		It("clears the visualization when Contents is empty", [this]()
		{
			FScopedTestWorld TW(TEXT("BucketSpec_Empty"));
			ABucket* Bucket = SpawnBucket(TW.World);
			if (!Bucket) return;

			Bucket->Contents = { 1, 2 };
			Bucket->RefreshContents();
			Bucket->Contents.Reset();
			Bucket->RefreshContents();

			TestEqual(TEXT("no balls after empty refresh"), Bucket->NumberBalls.Num(), 0);
		});

		It("rebuilds without leaking when called twice", [this]()
		{
			FScopedTestWorld TW(TEXT("BucketSpec_Rebuild"));
			ABucket* Bucket = SpawnBucket(TW.World);
			if (!Bucket) return;

			Bucket->Contents = { 11, 13, 17, 19, 23 };
			Bucket->RefreshContents();
			Bucket->Contents = { 2, 3 };
			Bucket->RefreshContents();

			TestEqual(TEXT("ball count matches latest Contents"), Bucket->NumberBalls.Num(), 2);
		});
	});

	Describe("BilliardBallMaterial", [this]()
	{
		It("applies a UMaterialInstanceDynamic to each ball when material is set", [this]()
		{
			FScopedTestWorld TW(TEXT("BucketSpec_Billiard_Set"));
			ABucket* Bucket = SpawnBucket(TW.World);
			if (!Bucket) return;

			UMaterialInterface* StubMaterial = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			TestNotNull(TEXT("stub material loaded"), StubMaterial);
			if (!StubMaterial) return;
			Bucket->BilliardBallMaterial = StubMaterial;

			Bucket->Contents = { 1, 2, 3 };
			Bucket->RefreshContents();

			TestEqual(TEXT("3 balls"), Bucket->NumberBalls.Num(), 3);
			for (int32 i = 0; i < Bucket->NumberBalls.Num(); ++i)
			{
				UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(
					Bucket->NumberBalls[i]->GetMaterial(0));
				TestNotNull(*FString::Printf(TEXT("ball %d has MID"), i), MID);
			}
		});

		It("leaves the default material on each ball when BilliardBallMaterial is unset", [this]()
		{
			FScopedTestWorld TW(TEXT("BucketSpec_Billiard_Unset"));
			ABucket* Bucket = SpawnBucket(TW.World);
			if (!Bucket) return;

			Bucket->Contents = { 7, 11 };
			Bucket->RefreshContents();

			TestEqual(TEXT("2 balls"), Bucket->NumberBalls.Num(), 2);
			for (int32 i = 0; i < Bucket->NumberBalls.Num(); ++i)
			{
				UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(
					Bucket->NumberBalls[i]->GetMaterial(0));
				TestNull(*FString::Printf(TEXT("ball %d uses default (no MID)"), i), MID);
			}
		});
	});

	Describe("HighlightBallsAtIndices (Story 25 — filter selection preview)", [this]()
	{
		It("only the named indices get an EmissiveMeshMaterial MID; others untouched", [this]()
		{
			FScopedTestWorld TW(TEXT("BucketSpec_HighlightSubset"));
			ABucket* Bucket = SpawnBucket(TW.World);
			if (!Bucket) return;

			Bucket->Contents = { 3, 5, 7 };
			Bucket->RefreshContents();
			Bucket->HighlightBallsAtIndices({ 0, 2 });

			UMaterialInterface* Emissive = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
			TestNotNull(TEXT("EmissiveMeshMaterial loaded"), Emissive);
			if (!Emissive) return;

			TestEqual(TEXT("3 balls"), Bucket->NumberBalls.Num(), 3);

			UMaterialInstanceDynamic* MID0 = Cast<UMaterialInstanceDynamic>(
				Bucket->NumberBalls[0]->GetMaterial(0));
			TestNotNull(TEXT("ball 0 highlighted MID present"), MID0);
			if (MID0) TestEqual(TEXT("ball 0 MID parent is EmissiveMeshMaterial"),
				MID0->Parent.Get(), Emissive);

			UMaterialInstanceDynamic* MID1 = Cast<UMaterialInstanceDynamic>(
				Bucket->NumberBalls[1]->GetMaterial(0));
			if (MID1)
			{
				TestNotEqual(TEXT("ball 1 NOT EmissiveMeshMaterial"),
					MID1->Parent.Get(), Emissive);
			}

			UMaterialInstanceDynamic* MID2 = Cast<UMaterialInstanceDynamic>(
				Bucket->NumberBalls[2]->GetMaterial(0));
			TestNotNull(TEXT("ball 2 highlighted MID present"), MID2);
			if (MID2) TestEqual(TEXT("ball 2 MID parent is EmissiveMeshMaterial"),
				MID2->Parent.Get(), Emissive);
		});

		It("empty Indices is a no-op — no ball gets EmissiveMeshMaterial", [this]()
		{
			FScopedTestWorld TW(TEXT("BucketSpec_HighlightEmpty"));
			ABucket* Bucket = SpawnBucket(TW.World);
			if (!Bucket) return;

			Bucket->Contents = { 1, 2 };
			Bucket->RefreshContents();
			Bucket->HighlightBallsAtIndices({});

			UMaterialInterface* Emissive = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
			for (UStaticMeshComponent* Ball : Bucket->NumberBalls)
			{
				UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Ball->GetMaterial(0));
				if (MID && Emissive)
				{
					TestNotEqual(TEXT("no ball got EmissiveMeshMaterial"),
						MID->Parent.Get(), Emissive);
				}
			}
		});
	});

	Describe("CloneIntoWorld (Story 31c — fan-out)", [this]()
	{
		It("returns a distinct ABucket actor with the source's Contents copied", [this]()
		{
			FScopedTestWorld TW(TEXT("BucketSpec_CloneIntoWorld_Distinct"));
			ABucket* Source = SpawnBucket(TW.World);
			if (!Source) return;

			Source->Contents = { 3, 5, 7 };
			Source->RefreshContents();

			ABucket* Clone = Source->CloneIntoWorld(TW.World, FVector(123.f, 0.f, 0.f));
			TestNotNull(TEXT("clone returned"), Clone);
			if (!Clone) return;

			TestTrue(TEXT("clone is a distinct actor"),  Clone != Source);
			TestEqual(TEXT("clone Contents.Num"),        Clone->Contents.Num(), 3);
			TestEqual(TEXT("clone Contents[0]"),         Clone->Contents[0], 3);
			TestEqual(TEXT("clone Contents[1]"),         Clone->Contents[1], 5);
			TestEqual(TEXT("clone Contents[2]"),         Clone->Contents[2], 7);
			TestEqual(TEXT("source Contents preserved"), Source->Contents.Num(), 3);
		});

		It("propagates BilliardBallMaterial to the clone", [this]()
		{
			FScopedTestWorld TW(TEXT("BucketSpec_CloneIntoWorld_Material"));
			ABucket* Source = SpawnBucket(TW.World);
			if (!Source) return;

			UMaterialInterface* StubMaterial = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			if (!StubMaterial) return;
			Source->BilliardBallMaterial = StubMaterial;
			Source->Contents = { 1 };
			Source->RefreshContents();

			ABucket* Clone = Source->CloneIntoWorld(TW.World, FVector::ZeroVector);
			TestNotNull(TEXT("clone returned"), Clone);
			if (!Clone) return;

			TestTrue(TEXT("clone has the same BilliardBallMaterial soft ref"),
				Clone->BilliardBallMaterial.ToSoftObjectPath()
					== Source->BilliardBallMaterial.ToSoftObjectPath());
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
