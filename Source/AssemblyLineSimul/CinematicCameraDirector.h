#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AssemblyLineTypes.h"
#include "DAG/AssemblyLineDAG.h"  // FNodeRef in event signatures (Story 36)
#include "CinematicCameraDirector.generated.h"

class APayloadCarrier;
class ACameraActor;
class UAssemblyLineDirector;
class UInputAction;
class UInputMappingContext;

// One fixed shot in the cinematic — Story 36 keeps this only for the
// wide overview (no subject; absolute world position). Per-station
// closeups are gone; replaced by the FollowSubject + FFramingSequence
// mechanism below.
USTRUCT(BlueprintType)
struct FCinematicShot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Shot")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Shot")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, Category = "Shot")
	float FieldOfView = 85.f;

	UPROPERTY(EditAnywhere, Category = "Shot")
	float HoldDuration = 5.f;

	UPROPERTY(EditAnywhere, Category = "Shot")
	float BlendDuration = 1.5f;
};

// Story 36 — one beat in the framing-sequence "zoom dance" played
// during a Working window. Time is seconds since FollowingBucket mode
// entered. Offset is added to the subject's world position to compute
// the camera's location each tick.
USTRUCT(BlueprintType)
struct FFramingKeyframe
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Framing")
	float Time = 0.f;

	UPROPERTY(EditAnywhere, Category = "Framing")
	FVector Offset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Framing")
	float FOV = 60.f;

	UPROPERTY(EditAnywhere, Category = "Framing")
	float BlendTime = 1.0f;
};

// USTRUCT wrapper around TArray<FFramingKeyframe> so it can be a TMap
// value (UPROPERTY TMap reflection requires both K and V to be
// individually-reflectable types).
USTRUCT(BlueprintType)
struct FFramingSequence
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Framing")
	TArray<FFramingKeyframe> Keyframes;
};

// Story 36 — internal mode for the cinematic's tick-driven follow logic.
UENUM()
enum class ECinematicMode : uint8
{
	WideOverview,    // fixed shot at line centroid; no subject
	FollowingBucket, // subject = active bucket; framing from sequence keyframes
	ChasingBucket,   // subject = rejected/accepted bucket; fixed offset (existing chase)
};

UCLASS()
class ASSEMBLYLINESIMUL_API ACinematicCameraDirector : public AActor
{
	GENERATED_BODY()

public:
	ACinematicCameraDirector();

	// The wide overview lives here as Shots[0]. Per-station closeups
	// are gone — replaced by the follow camera + framing sequence.
	UPROPERTY(EditAnywhere, Category = "Cinematic")
	TArray<FCinematicShot> Shots;

	UPROPERTY(EditAnywhere, Category = "Cinematic")
	int32 ResumeShotIndex = 0;

	// On OnStationIdle, hold the close-on-bucket framing this many
	// seconds before blending back to the wide overview. Default 0
	// keeps tests deterministic; production GameMode bumps this for a
	// more relaxed pacing.
	UPROPERTY(EditAnywhere, Category = "Cinematic")
	float LingerSecondsAfterIdle = 0.f;

	// Story 36 — default zoom-dance for any Working window without a
	// per-Kind override. Authored at SpawnCinematicDirector time.
	UPROPERTY(EditAnywhere, Category = "Cinematic|Framing")
	FFramingSequence DefaultFollowSequence;

	// Story 36 — optional per-Kind override. Falls back to
	// DefaultFollowSequence when a Kind isn't keyed here.
	UPROPERTY(EditAnywhere, Category = "Cinematic|Framing")
	TMap<EStationType, FFramingSequence> FramingByKind;

	void Start();
	void Stop();

	// Subscribe to AssemblyLineDirector's checker / cycle events. Idempotent.
	void BindToAssemblyLine(UAssemblyLineDirector* Director);

	// Story 16 — chase mode. When the Checker rejects, the camera follows the
	// rejected bucket back through the pipeline until the rework station's worker
	// enters Working (whereupon HandleStationActive jumps to that station's
	// closeup, ending the chase).
	bool IsChasingBucket() const { return Mode == ECinematicMode::ChasingBucket; }
	APayloadCarrier* GetChaseTarget() const;

	// Story 36 — public introspection for tests + debugging.
	ECinematicMode GetMode() const { return Mode; }
	AActor* GetFollowSubject() const { return FollowSubject.Get(); }
	ACameraActor* GetFollowCamera() const { return FollowCamera; }

	// Story 36 — manual transitions used by test code to drive the
	// state machine without relying on engine event timing.
	void EnterFollowingBucket(AActor* Subject, EStationType Kind);
	void EnterChase(APayloadCarrier* Bucket);
	void EnterWideOverview();

	// Public (was private) so the GameMode's Spawn flow can pre-populate
	// the wide overview shot before Start.
	void EnsureShotCameras();

	// Story 36 — Tick is public so tests can drive the follow update
	// deterministically (D->Tick(0.05f)) without depending on world tick
	// scheduling. Override of AActor::Tick.
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// Story 36 — the camera director's mode replaces the implicit
	// "either we're playing fixed shots or we're chasing" duality.
	ECinematicMode Mode = ECinematicMode::WideOverview;

	// Wide-overview shot bookkeeping (replaces CurrentShotIndex +
	// ShotTimer + per-station closeup arrays).
	int32 CurrentShotIndex = 0;
	FTimerHandle IdleLingerTimer;

	// Story 36 — single follow camera shared by FollowingBucket and
	// ChasingBucket modes. Spawned lazily; reused across cycles.
	UPROPERTY()
	TObjectPtr<ACameraActor> FollowCamera;

	// Story 36 — the subject the FollowCamera tracks each tick.
	// TWeakObjectPtr so a destroyed bucket cleanly drops us back to
	// WideOverview without stale-pointer crashes.
	UPROPERTY()
	TWeakObjectPtr<AActor> FollowSubject;

	// Story 36 — the framing sequence active for the current
	// FollowingBucket window (resolved per-Kind at entry).
	UPROPERTY()
	FFramingSequence ActiveSequence;

	// Story 36 — self-managed elapsed counter (seconds since FollowingBucket
	// entered). Reset to 0 on EnterFollowingBucket, incremented by
	// DeltaSeconds in Tick. Self-managed (not derived from
	// World->GetTimeSeconds) so tests can drive D->Tick(dt) deterministically
	// without depending on the world's tick clock.
	float ElapsedInFollowMode = 0.f;

	// Cached "previous interpolated framing" so blends look smooth even
	// when the active keyframe changes mid-blend.
	FVector LastAppliedOffset = FVector::ZeroVector;
	float   LastAppliedFOV    = 60.f;

	// Wide-overview shot cameras (Shots[]). Story 36 — usually just one.
	UPROPERTY()
	TArray<TObjectPtr<ACameraActor>> ShotCameras;

	UPROPERTY()
	TWeakObjectPtr<UAssemblyLineDirector> BoundDirector;

	FDelegateHandle CheckerStartedHandle;
	FDelegateHandle CycleCompletedHandle;
	FDelegateHandle CycleRejectedHandle;
	FDelegateHandle StationActiveHandle;
	FDelegateHandle StationIdleHandle;

	UPROPERTY()
	TObjectPtr<UInputMappingContext> SkipMappingContext;

	UPROPERTY()
	TObjectPtr<UInputAction> SkipAction;

	void JumpToWideOverview();
	void HandleCheckerStarted();
	void HandleCycleResumed(APayloadCarrier* Bucket);
	void HandleCycleRejected(APayloadCarrier* Bucket);
	void HandleStationActive(const FNodeRef& Ref);
	void HandleStationIdle(const FNodeRef& Ref);
	void HandleSkipPressed();
	void SetupSkipBinding();
	void EnsureFollowCamera();
	void TickFollowCamera();
	const FFramingSequence& ResolveSequenceForKind(EStationType Kind) const;
};
