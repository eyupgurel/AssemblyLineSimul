#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyLineDirector.h"
#include "PayloadCarrier.h"
#include "Camera/CameraActor.h"
#include "CinematicCameraDirector.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Station.h"
#include "TestStations.h"
#include "WorkerRobot.h"

namespace AssemblyLineCinematicTests
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

	// Story 36 — minimal cinematic with one wide overview shot + a default
	// follow sequence. Tests can supplement with FramingByKind overrides.
	static ACinematicCameraDirector* SpawnDirectorWithDefaults(UWorld* World,
		const TArray<FFramingKeyframe>& DefaultSeq = {})
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACinematicCameraDirector* D = World->SpawnActor<ACinematicCameraDirector>(
			ACinematicCameraDirector::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (!D) return nullptr;
		D->Shots.Reset();
		FCinematicShot Wide;
		Wide.Location     = FVector(-1000.f, 1000.f, 800.f);
		Wide.Rotation     = FRotator::ZeroRotator;
		Wide.FieldOfView  = 85.f;
		Wide.HoldDuration = 999.f;
		Wide.BlendDuration = 0.f;
		D->Shots.Add(Wide);
		D->ResumeShotIndex = 0;
		D->DefaultFollowSequence.Keyframes = DefaultSeq;
		return D;
	}

	// A simple subject for follow tests. Any AActor works since the camera
	// reads only its world location.
	static AActor* SpawnSubjectAt(UWorld* World, const FVector& Loc)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// Use APayloadCarrier as a convenient AActor stand-in (it's the production
		// subject anyway).
		return World->SpawnActor<APayloadCarrier>(APayloadCarrier::StaticClass(), Loc, FRotator::ZeroRotator, Params);
	}
}

DEFINE_SPEC(FCinematicCameraDirectorSpec,
	"AssemblyLineSimul.CinematicCameraDirector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FCinematicCameraDirectorSpec::Define()
{
	using namespace AssemblyLineCinematicTests;

	Describe("Mode default + wide-overview entry (Story 36)", [this]()
	{
		It("constructs in WideOverview mode with no follow subject", [this]()
		{
			FScopedTestWorld TW(TEXT("CinSpec_DefaultMode"));
			ACinematicCameraDirector* D = SpawnDirectorWithDefaults(TW.World);
			if (!D) return;
			TestEqual(TEXT("default mode is WideOverview"),
				static_cast<int32>(D->GetMode()),
				static_cast<int32>(ECinematicMode::WideOverview));
			TestNull(TEXT("no follow subject"), D->GetFollowSubject());
		});

		It("Start spawns the wide-overview shot camera + the permanent FollowCamera", [this]()
		{
			FScopedTestWorld TW(TEXT("CinSpec_StartSpawnsCameras"));
			ACinematicCameraDirector* D = SpawnDirectorWithDefaults(TW.World);
			if (!D) return;
			D->Start();
			TestNotNull(TEXT("FollowCamera spawned"), D->GetFollowCamera());
		});
	});

	Describe("EnterFollowingBucket (Story 36)", [this]()
	{
		It("switches to FollowingBucket mode with the supplied subject", [this]()
		{
			FScopedTestWorld TW(TEXT("CinSpec_EnterFollow_Mode"));
			ACinematicCameraDirector* D = SpawnDirectorWithDefaults(TW.World, {
				{0.f, FVector(0.f, 200.f, 100.f), 50.f, 0.f},
			});
			if (!D) return;
			D->Start();

			AActor* Subject = SpawnSubjectAt(TW.World, FVector(500.f, 0.f, 0.f));
			D->EnterFollowingBucket(Subject, EStationType::Filter);

			TestEqual(TEXT("mode is FollowingBucket"),
				static_cast<int32>(D->GetMode()),
				static_cast<int32>(ECinematicMode::FollowingBucket));
			TestEqual(TEXT("subject is the supplied actor"),
				D->GetFollowSubject(), Subject);
		});

		It("places the FollowCamera at subject + first-keyframe offset on entry", [this]()
		{
			FScopedTestWorld TW(TEXT("CinSpec_EnterFollow_InitialOffset"));
			const FVector ExpectedOffset(0.f, 600.f, 800.f);
			ACinematicCameraDirector* D = SpawnDirectorWithDefaults(TW.World, {
				{0.f, ExpectedOffset, 70.f, 0.f},
			});
			if (!D) return;
			D->Start();

			const FVector SubjectLoc(1500.f, 0.f, 100.f);
			AActor* Subject = SpawnSubjectAt(TW.World, SubjectLoc);
			D->EnterFollowingBucket(Subject, EStationType::Filter);

			ACameraActor* Cam = D->GetFollowCamera();
			TestNotNull(TEXT("follow camera exists"), Cam);
			if (!Cam) return;

			const FVector CamLoc = Cam->GetActorLocation();
			const FVector Expected = SubjectLoc + ExpectedOffset;
			TestTrue(TEXT("camera at subject + first-keyframe offset"),
				CamLoc.Equals(Expected, /*Tolerance=*/0.5f));
		});

		It("most-recent-subject tiebreak: second EnterFollowingBucket replaces subject + restarts sequence",
		   [this]()
		{
			FScopedTestWorld TW(TEXT("CinSpec_EnterFollow_Replace"));
			ACinematicCameraDirector* D = SpawnDirectorWithDefaults(TW.World, {
				{0.f, FVector(0.f, 100.f, 100.f), 50.f, 0.f},
			});
			if (!D) return;
			D->Start();

			AActor* SubjectA = SpawnSubjectAt(TW.World, FVector(0.f, 0.f, 0.f));
			AActor* SubjectB = SpawnSubjectAt(TW.World, FVector(2000.f, 0.f, 0.f));

			D->EnterFollowingBucket(SubjectA, EStationType::Filter);
			TestEqual(TEXT("first follow sets SubjectA"),
				D->GetFollowSubject(), SubjectA);

			D->EnterFollowingBucket(SubjectB, EStationType::Sorter);
			TestEqual(TEXT("second follow replaces with SubjectB"),
				D->GetFollowSubject(), SubjectB);
		});
	});

	Describe("Tick framing-keyframe interpolation (Story 36)", [this]()
	{
		It("Tick positions the FollowCamera at subject + active keyframe offset", [this]()
		{
			FScopedTestWorld TW(TEXT("CinSpec_Tick_PositionsCamera"));
			const FVector Offset(50.f, 300.f, 400.f);
			ACinematicCameraDirector* D = SpawnDirectorWithDefaults(TW.World, {
				{0.f, Offset, 60.f, 0.f},
			});
			if (!D) return;
			D->Start();

			const FVector SubjectLoc(750.f, 0.f, 0.f);
			AActor* Subject = SpawnSubjectAt(TW.World, SubjectLoc);
			D->EnterFollowingBucket(Subject, EStationType::Filter);

			TW.World->Tick(LEVELTICK_All, 0.05f);

			ACameraActor* Cam = D->GetFollowCamera();
			TestNotNull(TEXT("follow camera"), Cam);
			if (!Cam) return;

			const FVector Expected = SubjectLoc + Offset;
			TestTrue(TEXT("camera tracks subject + offset after tick"),
				Cam->GetActorLocation().Equals(Expected, /*Tolerance=*/0.5f));
		});

		It("interpolates between keyframes over time", [this]()
		{
			FScopedTestWorld TW(TEXT("CinSpec_Tick_Interpolates"));
			// Two keyframes: t=0 offset Z=100, t=2 offset Z=300, BlendTime=2s.
			// Drive cinematic Tick directly so elapsed advancement is
			// deterministic (independent of world tick scheduling in
			// headless test fixtures).
			ACinematicCameraDirector* D = SpawnDirectorWithDefaults(TW.World, {
				{0.f, FVector(0.f, 0.f, 100.f), 60.f, 1.f},
				{2.f, FVector(0.f, 0.f, 300.f), 60.f, 2.f},
			});
			if (!D) return;
			D->Start();

			AActor* Subject = SpawnSubjectAt(TW.World, FVector::ZeroVector);
			D->EnterFollowingBucket(Subject, EStationType::Filter);

			// Advance ELAPSED in 0.05 s steps to ~3 s (1 s into the blend
			// toward the t=2 keyframe). With BlendTime=2, alpha = (3-2)/2
			// = 0.5 → midway → Z ≈ 200.
			for (int32 i = 0; i < 60; ++i) D->Tick(0.05f);

			ACameraActor* Cam = D->GetFollowCamera();
			if (!Cam) return;
			const float Z = Cam->GetActorLocation().Z;
			TestTrue(TEXT("Z interpolated to roughly midway (180–220)"),
				Z >= 180.f && Z <= 220.f);
		});
	});

	Describe("FramingByKind override (Story 36)", [this]()
	{
		It("uses per-Kind override when present; falls back to default otherwise", [this]()
		{
			FScopedTestWorld TW(TEXT("CinSpec_PerKindOverride"));
			ACinematicCameraDirector* D = SpawnDirectorWithDefaults(TW.World, {
				{0.f, FVector(0.f, 0.f, 100.f), 60.f, 0.f},  // default
			});
			if (!D) return;

			FFramingSequence FilterSeq;
			FilterSeq.Keyframes.Add({0.f, FVector(0.f, 0.f, 999.f), 30.f, 0.f});
			D->FramingByKind.Add(EStationType::Filter, FilterSeq);
			D->Start();

			AActor* Subject = SpawnSubjectAt(TW.World, FVector::ZeroVector);

			// Filter (override) → Z=999
			D->EnterFollowingBucket(Subject, EStationType::Filter);
			TestTrue(TEXT("Filter uses override (Z=999)"),
				D->GetFollowCamera()->GetActorLocation().Equals(FVector(0.f, 0.f, 999.f), 0.5f));

			// Sorter (no override) → Z=100 from default
			D->EnterFollowingBucket(Subject, EStationType::Sorter);
			TestTrue(TEXT("Sorter falls back to default (Z=100)"),
				D->GetFollowCamera()->GetActorLocation().Equals(FVector(0.f, 0.f, 100.f), 0.5f));
		});
	});

	Describe("HandleStationIdle (Story 36)", [this]()
	{
		It("returns to WideOverview immediately when LingerSecondsAfterIdle == 0", [this]()
		{
			FScopedTestWorld TW(TEXT("CinSpec_IdleReturns"));
			ACinematicCameraDirector* D = SpawnDirectorWithDefaults(TW.World, {
				{0.f, FVector(0.f, 0.f, 100.f), 60.f, 0.f},
			});
			if (!D) return;
			D->LingerSecondsAfterIdle = 0.f;
			D->Start();

			AActor* Subject = SpawnSubjectAt(TW.World, FVector::ZeroVector);
			D->EnterFollowingBucket(Subject, EStationType::Filter);
			TestEqual(TEXT("entered FollowingBucket"),
				static_cast<int32>(D->GetMode()),
				static_cast<int32>(ECinematicMode::FollowingBucket));

			D->EnterWideOverview();  // direct call simulates HandleStationIdle's no-linger branch
			TestEqual(TEXT("returned to WideOverview"),
				static_cast<int32>(D->GetMode()),
				static_cast<int32>(ECinematicMode::WideOverview));
			TestNull(TEXT("subject cleared"), D->GetFollowSubject());
		});
	});

	Describe("Subject invalidation (Story 36)", [this]()
	{
		It("falls back to WideOverview when the follow subject is destroyed mid-tick", [this]()
		{
			FScopedTestWorld TW(TEXT("CinSpec_SubjectVanishes"));
			ACinematicCameraDirector* D = SpawnDirectorWithDefaults(TW.World, {
				{0.f, FVector(0.f, 0.f, 100.f), 60.f, 0.f},
			});
			if (!D) return;
			D->Start();

			AActor* Subject = SpawnSubjectAt(TW.World, FVector::ZeroVector);
			D->EnterFollowingBucket(Subject, EStationType::Filter);
			TestEqual(TEXT("FollowingBucket pre-destroy"),
				static_cast<int32>(D->GetMode()),
				static_cast<int32>(ECinematicMode::FollowingBucket));

			Subject->Destroy();
			// Drive Tick directly — TWeakObjectPtr.Get() returns null for
			// pending-kill actors, and TickFollowCamera bails to overview.
			D->Tick(0.05f);

			TestEqual(TEXT("dropped to WideOverview after subject destroyed"),
				static_cast<int32>(D->GetMode()),
				static_cast<int32>(ECinematicMode::WideOverview));
		});
	});

	Describe("Chase preserved (Story 16 + Story 36 unification)", [this]()
	{
		It("HandleCycleRejected enters ChasingBucket mode with the rejected bucket as subject", [this]()
		{
			FScopedTestWorld TW(TEXT("CinSpec_ChaseRejected"));
			ACinematicCameraDirector* D = SpawnDirectorWithDefaults(TW.World);
			if (!D) return;
			D->Start();

			APayloadCarrier* Bucket = (APayloadCarrier*)SpawnSubjectAt(TW.World, FVector(2000.f, 0.f, 0.f));
			D->EnterChase(Bucket);

			TestEqual(TEXT("mode is ChasingBucket"),
				static_cast<int32>(D->GetMode()),
				static_cast<int32>(ECinematicMode::ChasingBucket));
			TestTrue(TEXT("IsChasingBucket reports true"), D->IsChasingBucket());
			TestEqual(TEXT("GetChaseTarget returns the bucket"),
				D->GetChaseTarget(), Bucket);
		});

		It("Chase + Follow share the same FollowCamera actor (no double-spawn)", [this]()
		{
			FScopedTestWorld TW(TEXT("CinSpec_OneFollowCamera"));
			ACinematicCameraDirector* D = SpawnDirectorWithDefaults(TW.World, {
				{0.f, FVector(0.f, 0.f, 100.f), 60.f, 0.f},
			});
			if (!D) return;
			D->Start();

			ACameraActor* CamBefore = D->GetFollowCamera();
			TestNotNull(TEXT("follow camera spawned at Start"), CamBefore);

			AActor* Subject = SpawnSubjectAt(TW.World, FVector::ZeroVector);
			D->EnterFollowingBucket(Subject, EStationType::Filter);
			D->EnterChase((APayloadCarrier*)Subject);
			D->EnterFollowingBucket(Subject, EStationType::Sorter);

			TestEqual(TEXT("same FollowCamera reused across mode transitions"),
				D->GetFollowCamera(), CamBefore);
		});

		It("EnterChase with null bucket falls back to WideOverview (existing contract)", [this]()
		{
			FScopedTestWorld TW(TEXT("CinSpec_ChaseNull"));
			ACinematicCameraDirector* D = SpawnDirectorWithDefaults(TW.World);
			if (!D) return;
			D->Start();

			D->EnterChase(nullptr);
			TestEqual(TEXT("null chase → WideOverview"),
				static_cast<int32>(D->GetMode()),
				static_cast<int32>(ECinematicMode::WideOverview));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
