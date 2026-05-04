#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSingleNodeInstance.h"
#include "Bucket.h"
#include "Station.h"
#include "TestStations.h"
#include "WorkerRobot.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace AssemblyLineSimulTests
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FWorldContext* Context = nullptr;

		FScopedTestWorld(const TCHAR* Name)
		{
			World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false, FName(Name));
			Context = &GEngine->CreateNewWorldContext(EWorldType::Game);
			Context->SetCurrentWorld(World);

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

	template <typename TStation>
	static TStation* SpawnStationAt(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<TStation>(TStation::StaticClass(), Location, FRotator::ZeroRotator, Params);
	}

	static AWorkerRobot* SpawnWorkerAt(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AWorkerRobot>(AWorkerRobot::StaticClass(), Location, FRotator::ZeroRotator, Params);
	}

	static ABucket* SpawnBucketAt(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ABucket>(ABucket::StaticClass(), Location, FRotator::ZeroRotator, Params);
	}

	// Drive the worker FSM directly — World->Tick on a transient test world doesn't reliably
	// dispatch actor ticks. Calling AActor::Tick is exactly what the tick group would do.
	template <typename TPredicate>
	static int32 TickWorker(AWorkerRobot* Worker, int32 MaxTicks, float DeltaSeconds, TPredicate bDone)
	{
		for (int32 i = 0; i < MaxTicks; ++i)
		{
			if (bDone()) return i;
			Worker->Tick(DeltaSeconds);
		}
		return MaxTicks;
	}
}

DEFINE_SPEC(FWorkerRobotSpec,
	"AssemblyLineSimul.WorkerRobot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FWorkerRobotSpec::Define()
{
	using namespace AssemblyLineSimulTests;

	Describe("BodyMesh", [this]()
	{
		It("constructs with SkeletalBodyMesh hidden, composite robot parts visible, legacy primitives hidden", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_BodyMesh_Default"));
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, FVector::ZeroVector);

			TestNotNull(TEXT("SkeletalBodyMesh exists"), Worker->SkeletalBodyMesh.Get());
			TestFalse(TEXT("SkeletalBodyMesh hidden by default"), Worker->SkeletalBodyMesh->IsVisible());

			TestNotNull(TEXT("Torso exists"),    Worker->Torso.Get());
			TestNotNull(TEXT("HeadDome exists"), Worker->HeadDome.Get());
			TestNotNull(TEXT("Eye exists"),      Worker->Eye.Get());
			TestNotNull(TEXT("LeftArm exists"),  Worker->LeftArm.Get());
			TestNotNull(TEXT("RightArm exists"), Worker->RightArm.Get());
			TestNotNull(TEXT("Antenna exists"),  Worker->Antenna.Get());

			TestTrue(TEXT("Torso visible"),    Worker->Torso->IsVisible());
			TestTrue(TEXT("HeadDome visible"), Worker->HeadDome->IsVisible());
			TestTrue(TEXT("Eye visible"),      Worker->Eye->IsVisible());
			TestTrue(TEXT("LeftArm visible"),  Worker->LeftArm->IsVisible());
			TestTrue(TEXT("RightArm visible"), Worker->RightArm->IsVisible());
			TestTrue(TEXT("Antenna visible"),  Worker->Antenna->IsVisible());

			TestFalse(TEXT("Legacy BodyMesh hidden"), Worker->BodyMesh->IsVisible());
			TestFalse(TEXT("Legacy HeadMesh hidden"), Worker->HeadMesh->IsVisible());
		});

		It("ApplyBodyMesh assigns the mesh and hides every composite + legacy primitive", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_BodyMesh_Apply"));
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, FVector::ZeroVector);

			USkeletalMesh* TestMesh = NewObject<USkeletalMesh>(GetTransientPackage());
			Worker->ApplyBodyMesh(TestMesh);

			TestEqual(TEXT("SkeletalBodyMesh holds assigned mesh"),
				Worker->SkeletalBodyMesh->GetSkeletalMeshAsset(), TestMesh);
			TestTrue(TEXT("SkeletalBodyMesh visible after apply"), Worker->SkeletalBodyMesh->IsVisible());

			TestFalse(TEXT("Torso hidden after apply"),    Worker->Torso->IsVisible());
			TestFalse(TEXT("HeadDome hidden after apply"), Worker->HeadDome->IsVisible());
			TestFalse(TEXT("Eye hidden after apply"),      Worker->Eye->IsVisible());
			TestFalse(TEXT("LeftArm hidden after apply"),  Worker->LeftArm->IsVisible());
			TestFalse(TEXT("RightArm hidden after apply"), Worker->RightArm->IsVisible());
			TestFalse(TEXT("Antenna hidden after apply"),  Worker->Antenna->IsVisible());

			TestFalse(TEXT("Legacy BodyMesh hidden after apply"), Worker->BodyMesh->IsVisible());
			TestFalse(TEXT("Legacy HeadMesh hidden after apply"), Worker->HeadMesh->IsVisible());
		});

		It("ApplyBodyMesh(nullptr) is a no-op and leaves the composite parts visible", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_BodyMesh_Null"));
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, FVector::ZeroVector);

			Worker->ApplyBodyMesh(nullptr);

			TestNull(TEXT("SkeletalBodyMesh remains empty"), Worker->SkeletalBodyMesh->GetSkeletalMeshAsset());
			TestFalse(TEXT("SkeletalBodyMesh stays hidden"), Worker->SkeletalBodyMesh->IsVisible());
			TestTrue(TEXT("Torso still visible"),    Worker->Torso->IsVisible());
			TestTrue(TEXT("HeadDome still visible"), Worker->HeadDome->IsVisible());
		});
	});

	Describe("Phase events", [this]()
	{
		It("fires OnPickedUp with the assigned station's FNodeRef when reaching PickUp (Story 36)", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerSpec_OnPickedUp"));
			ATestSyncStation* Station = SpawnStationAt<ATestSyncStation>(TW.World, FVector::ZeroVector);
			Station->StationType = EStationType::Filter;
			Station->NodeRef    = FNodeRef{EStationType::Filter, 1};  // Story 36 — Instance 1 to prove FNodeRef carries through
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, FVector::ZeroVector);
			Worker->WorkDuration = 0.f;
			Worker->AssignStation(Station);
			Station->InputSlot->SetWorldLocation(Worker->GetActorLocation());
			Station->OutputSlot->SetWorldLocation(Worker->GetActorLocation());

			FNodeRef Captured;
			bool bFired = false;
			Worker->OnPickedUp.AddLambda([&Captured, &bFired](const FNodeRef& Ref)
			{
				Captured = Ref;
				bFired = true;
			});

			ABucket* Bucket = SpawnBucketAt(TW.World, FVector::ZeroVector);
			Worker->BeginTask(Bucket, Station->InputSlot, Station->OutputSlot, FWorkerTaskComplete());
			TickWorker(Worker, 50, 0.05f, [&]() { return bFired; });

			TestTrue(TEXT("OnPickedUp fired"), bFired);
			TestTrue(TEXT("FNodeRef carried (Filter, 1)"),
				Captured == FNodeRef{EStationType::Filter, 1});
		});

		It("fires OnPlaced with the assigned station's FNodeRef when reaching Place (Story 36)", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerSpec_OnPlaced"));
			ATestSyncStation* Station = SpawnStationAt<ATestSyncStation>(TW.World, FVector::ZeroVector);
			Station->StationType = EStationType::Sorter;
			Station->NodeRef    = FNodeRef{EStationType::Sorter, 0};
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, FVector::ZeroVector);
			Worker->WorkDuration = 0.f;
			Worker->AssignStation(Station);
			Station->InputSlot->SetWorldLocation(Worker->GetActorLocation());
			Station->OutputSlot->SetWorldLocation(Worker->GetActorLocation());

			FNodeRef Captured;
			bool bFired = false;
			Worker->OnPlaced.AddLambda([&Captured, &bFired](const FNodeRef& Ref)
			{
				Captured = Ref;
				bFired = true;
			});

			ABucket* Bucket = SpawnBucketAt(TW.World, FVector::ZeroVector);
			Worker->BeginTask(Bucket, Station->InputSlot, Station->OutputSlot, FWorkerTaskComplete());
			TickWorker(Worker, 100, 0.05f, [&]() { return bFired; });

			TestTrue(TEXT("OnPlaced fired"), bFired);
			TestTrue(TEXT("FNodeRef carried (Sorter, 0)"),
				Captured == FNodeRef{EStationType::Sorter, 0});
		});
	});

	Describe("ApplyTint", [this]()
	{
		It("creates a UMaterialInstanceDynamic at material 0 on every composite body part", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_Tint_Composite"));
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, FVector::ZeroVector);

			Worker->ApplyTint(FLinearColor::Red);

			auto AssertMID = [this](const TCHAR* Name, UStaticMeshComponent* Comp)
			{
				if (!Comp) { AddError(FString::Printf(TEXT("%s null"), Name)); return; }
				UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(0));
				TestNotNull(*FString::Printf(TEXT("%s has MID at material 0"), Name), MID);
			};
			AssertMID(TEXT("Torso"),    Worker->Torso);
			AssertMID(TEXT("HeadDome"), Worker->HeadDome);
			AssertMID(TEXT("Eye"),      Worker->Eye);
			AssertMID(TEXT("LeftArm"),  Worker->LeftArm);
			AssertMID(TEXT("RightArm"), Worker->RightArm);
			AssertMID(TEXT("Antenna"),  Worker->Antenna);
		});
	});

	Describe("Visual scale (Story 18 AC18.1)", [this]()
	{
		It("spawns at 1.5x actor scale so the mannequin reads at human size", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_Scale"));
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, FVector::ZeroVector);
			TestEqual(TEXT("uniform 1.5x scale"),
				Worker->GetActorScale3D(), FVector(1.5f, 1.5f, 1.5f));
		});
	});

	Describe("Animation by state (Story 18 AC18.2 / AC18.3)", [this]()
	{
		It("PickAnimationForState returns IdleAnimation for stationary states "
		   "(Idle, Working) — PickUp and Place are transient chain-states.", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_AnimIdle"));
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, FVector::ZeroVector);
			TestNotNull(TEXT("IdleAnimation loaded"), Worker->IdleAnimation.Get());

			for (EWorkerState S : { EWorkerState::Idle, EWorkerState::Working })
			{
				TestEqual(*FString::Printf(TEXT("state %d → idle anim"), (int32)S),
					Worker->PickAnimationForState(S), Worker->IdleAnimation.Get());
			}
		});

		It("PickAnimationForState returns WalkAnimation for moving states "
		   "(MoveToInput, MoveToWorkPos, MoveToOutput, ReturnHome)", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_AnimWalk"));
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, FVector::ZeroVector);
			TestNotNull(TEXT("WalkAnimation loaded"), Worker->WalkAnimation.Get());

			for (EWorkerState S : { EWorkerState::MoveToInput, EWorkerState::MoveToWorkPos,
			                        EWorkerState::MoveToOutput, EWorkerState::ReturnHome })
			{
				TestEqual(*FString::Printf(TEXT("state %d → walk anim"), (int32)S),
					Worker->PickAnimationForState(S), Worker->WalkAnimation.Get());
			}
		});

		It("starts playing the chosen animation looped after ApplyBodyMesh assigns "
		   "a real skeletal mesh — SetAnimation alone leaves the SingleNodeInstance "
		   "stopped, this asserts Play(true) is invoked too", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_AnimPlays"));
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, FVector::ZeroVector);
			USkeletalMesh* Manny = LoadObject<USkeletalMesh>(nullptr,
				TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
			TestNotNull(TEXT("loaded SKM_Manny_Simple"), Manny);
			if (!Manny) return;

			Worker->ApplyBodyMesh(Manny);

			UAnimSingleNodeInstance* Inst = Worker->SkeletalBodyMesh->GetSingleNodeInstance();
			TestNotNull(TEXT("SingleNodeInstance exists after ApplyBodyMesh"), Inst);
			if (!Inst) return;
			TestEqual(TEXT("instance has the idle anim asset"),
				Inst->GetAnimationAsset(), Cast<UAnimationAsset>(Worker->IdleAnimation.Get()));
			TestTrue(TEXT("instance is playing"), Inst->IsPlaying());
			TestTrue(TEXT("instance is looping"), Inst->IsLooping());
		});
	});

	Describe("ActiveLight glow (Story 19)", [this]()
	{
		It("constructs with ActiveLight off (Intensity == 0) and a green LightColor", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_ActiveLightDefault"));
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, FVector::ZeroVector);

			TestNotNull(TEXT("ActiveLight component exists"), Worker->ActiveLight.Get());
			if (!Worker->ActiveLight) return;
			TestEqual(TEXT("intensity off by default"), Worker->ActiveLight->Intensity, 0.f);
			const FLinearColor Color = Worker->ActiveLight->GetLightColor();
			TestTrue(TEXT("light is greenish (G > R and G > B)"),
				Color.G > Color.R && Color.G > Color.B);
		});

		It("SetActive(true) lights the worker (Intensity > 0); SetActive(false) turns it off", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_SetActive"));
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, FVector::ZeroVector);
			if (!Worker || !Worker->ActiveLight) return;

			Worker->SetActive(true);
			TestTrue(TEXT("intensity > 0 after SetActive(true)"),
				Worker->ActiveLight->Intensity > 0.f);

			Worker->SetActive(false);
			TestEqual(TEXT("intensity back to 0 after SetActive(false)"),
				Worker->ActiveLight->Intensity, 0.f);
		});
	});

	Describe("BeginTask", [this]()
	{
		It("completes the FSM exactly once when the station's ProcessBucket fires synchronously", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_Sync"));

			const FVector Origin(0.f, 0.f, 0.f);
			ATestSyncStation* Station = SpawnStationAt<ATestSyncStation>(TW.World, Origin);
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, Origin);
			ABucket* Bucket = SpawnBucketAt(TW.World, Origin);

			Worker->WorkDuration = 0.f;
			Worker->AssignStation(Station);
			Station->InputSlot->SetWorldLocation(Worker->GetActorLocation());
			Station->OutputSlot->SetWorldLocation(Worker->GetActorLocation());

			int32 CompleteCount = 0;
			Worker->BeginTask(Bucket, Station->InputSlot, Station->OutputSlot,
				FWorkerTaskComplete::CreateLambda([&CompleteCount](ABucket*) { ++CompleteCount; }));

			TickWorker(Worker, 100, 0.05f, [&]() { return CompleteCount > 0; });

			TestEqual(TEXT("ProcessBucket call count"), Station->ProcessCallCount, 1);
			TestEqual(TEXT("TaskComplete callback count"), CompleteCount, 1);
		});

		It("invokes ProcessBucket exactly once and waits for the deferred completion", [this]()
		{
			FScopedTestWorld TW(TEXT("WorkerRobotSpec_Deferred"));

			const FVector Origin(0.f, 0.f, 0.f);
			ATestDeferredStation* Station = SpawnStationAt<ATestDeferredStation>(TW.World, Origin);
			AWorkerRobot* Worker = SpawnWorkerAt(TW.World, Origin);
			ABucket* Bucket = SpawnBucketAt(TW.World, Origin);

			Worker->WorkDuration = 0.f;
			Worker->AssignStation(Station);
			Station->InputSlot->SetWorldLocation(Worker->GetActorLocation());
			Station->OutputSlot->SetWorldLocation(Worker->GetActorLocation());

			int32 CompleteCount = 0;
			Worker->BeginTask(Bucket, Station->InputSlot, Station->OutputSlot,
				FWorkerTaskComplete::CreateLambda([&CompleteCount](ABucket*) { ++CompleteCount; }));

			// Tick enough frames for the worker to enter Working and dispatch ProcessBucket once.
			TickWorker(Worker, 20, 0.05f, [&]() { return Station->ProcessCallCount > 0; });

			TestEqual(TEXT("ProcessBucket called once after dispatch"), Station->ProcessCallCount, 1);
			TestEqual(TEXT("TaskComplete has not fired yet"), CompleteCount, 0);

			// Continue ticking; ProcessBucket must NOT be re-invoked while we hold the delegate.
			TickWorker(Worker, 20, 0.05f, [&]() { return false; });
			TestEqual(TEXT("ProcessBucket still called only once during wait"), Station->ProcessCallCount, 1);

			// Fire the captured delegate; worker should complete the FSM.
			Station->FireCapturedDelegate();
			TickWorker(Worker, 100, 0.05f, [&]() { return CompleteCount > 0; });

			TestEqual(TEXT("ProcessBucket call count after completion"), Station->ProcessCallCount, 1);
			TestEqual(TEXT("TaskComplete fired exactly once"), CompleteCount, 1);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
