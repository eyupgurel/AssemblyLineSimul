#pragma once

#include "CoreMinimal.h"
#include "Bucket.h"
#include "Station.h"
#include "TestStations.generated.h"

UCLASS(NotBlueprintable, NotPlaceable, HideDropdown)
class ATestBucketSubclass : public ABucket
{
	GENERATED_BODY()
};

// Fires the completion delegate inside the call.
UCLASS(NotBlueprintable, NotPlaceable, HideDropdown)
class ATestSyncStation : public AStation
{
	GENERATED_BODY()
public:
	int32 ProcessCallCount = 0;

	// Story 31d — captured so specs can assert on the multi-input array
	// passed to a fan-in merge.
	TArray<TWeakObjectPtr<ABucket>> LastInputs;

	virtual void ProcessBucket(const TArray<ABucket*>& Inputs, FStationProcessComplete OnComplete) override
	{
		++ProcessCallCount;
		LastInputs.Reset();
		for (ABucket* B : Inputs) LastInputs.Add(B);
		FStationProcessResult Result;
		Result.bAccepted = true;
		OnComplete.ExecuteIfBound(Result);
	}
};

// Captures the completion delegate; the test fires it manually.
UCLASS(NotBlueprintable, NotPlaceable, HideDropdown)
class ATestDeferredStation : public AStation
{
	GENERATED_BODY()
public:
	int32 ProcessCallCount = 0;
	FStationProcessComplete CapturedDelegate;

	virtual void ProcessBucket(const TArray<ABucket*>& Inputs, FStationProcessComplete OnComplete) override
	{
		++ProcessCallCount;
		CapturedDelegate = OnComplete;
	}

	void FireCapturedDelegate(bool bAccepted = true)
	{
		FStationProcessResult Result;
		Result.bAccepted = bAccepted;
		CapturedDelegate.ExecuteIfBound(Result);
		CapturedDelegate.Unbind();
	}
};
