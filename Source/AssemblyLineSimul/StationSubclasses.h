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
	int32 BucketSize = 20;

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
};
