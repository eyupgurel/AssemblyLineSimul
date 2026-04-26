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

	virtual void ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete) override;
};

UCLASS()
class ASSEMBLYLINESIMUL_API AFilterStation : public AStation
{
	GENERATED_BODY()
public:
	AFilterStation();
	virtual void ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete) override;
};

UCLASS()
class ASSEMBLYLINESIMUL_API ASorterStation : public AStation
{
	GENERATED_BODY()
public:
	ASorterStation();
	virtual void ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete) override;
};

UCLASS()
class ASSEMBLYLINESIMUL_API ACheckerStation : public AStation
{
	GENERATED_BODY()
public:
	ACheckerStation();
	virtual void ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete) override;

	// When true, GetEffectiveRule composes Generator + Filter + Sorter rules at read time
	// so chat-driven changes upstream automatically reach the Checker. Flipped to false
	// the first time the user gives the Checker its own rule via chat; toggle in the editor
	// to revert.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checker")
	bool bUseDerivedRule = true;

	virtual FString GetEffectiveRule() const override;
	virtual void OnRuleSetByChat() override;
};
