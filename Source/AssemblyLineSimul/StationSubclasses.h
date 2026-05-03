#pragma once

#include "CoreMinimal.h"
#include "Station.h"
#include "StationSubclasses.generated.h"

UCLASS()
class ASSEMBLYLINESIMUL_API AGeneratorStation : public AStation
{
	GENERATED_BODY()
public:
	AGeneratorStation();

	UPROPERTY(EditAnywhere, Category = "Generator")
	int32 BucketSize = 10;

	UPROPERTY(EditAnywhere, Category = "Generator")
	int32 MinValue = 1;

	UPROPERTY(EditAnywhere, Category = "Generator")
	int32 MaxValue = 100;

	virtual void ProcessBucket(const TArray<ABucket*>& Inputs, FStationProcessComplete OnComplete) override;
};

UCLASS()
class ASSEMBLYLINESIMUL_API AFilterStation : public AStation
{
	GENERATED_BODY()
public:
	AFilterStation();
	virtual void ProcessBucket(const TArray<ABucket*>& Inputs, FStationProcessComplete OnComplete) override;

	// Story 25 — for each value in KeptValues (in order), finds its first
	// matching, not-already-claimed index in InputContents. Returns the matched
	// indices in the order the kept values were supplied. Used to drive the
	// 1-second selection-preview highlight on the bucket before the rejected
	// balls are dropped.
	static TArray<int32> FindKeptIndices(
		const TArray<int32>& InputContents, const TArray<int32>& KeptValues);
};

UCLASS()
class ASSEMBLYLINESIMUL_API ASorterStation : public AStation
{
	GENERATED_BODY()
public:
	ASorterStation();
	virtual void ProcessBucket(const TArray<ABucket*>& Inputs, FStationProcessComplete OnComplete) override;
};

UCLASS()
class ASSEMBLYLINESIMUL_API ACheckerStation : public AStation
{
	GENERATED_BODY()
public:
	ACheckerStation();
	virtual void ProcessBucket(const TArray<ABucket*>& Inputs, FStationProcessComplete OnComplete) override;

	// Public seam for the Claude completion handler — production code routes
	// through the lambda inside ProcessBucket; tests call this directly with
	// synthetic JSON to lock down the speak-on-PASS / speak-on-REJECT contract
	// without spinning up a real Claude HTTP round-trip.
	void HandleVerdictReply(bool bSuccess, const FString& Response, FStationProcessComplete OnComplete);

	// When true, GetEffectiveRule composes Generator + Filter + Sorter rules at read time
	// so chat-driven changes upstream automatically reach the Checker. Flipped to false
	// the first time the user gives the Checker its own rule via chat; toggle in the editor
	// to revert.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checker")
	bool bUseDerivedRule = true;

	virtual FString GetEffectiveRule() const override;
	virtual void OnRuleSetByChat() override;
};

// Story 32a — chat-only meta agent. Receives the operator's mission at boot
// and emits a DAG spec describing the line to spawn (parsed by
// OrchestratorParser). Never appears in a station's processing chain — its
// inherited ProcessBucket is unreachable.
UCLASS()
class ASSEMBLYLINESIMUL_API AOrchestratorStation : public AStation
{
	GENERATED_BODY()
public:
	AOrchestratorStation();
};
