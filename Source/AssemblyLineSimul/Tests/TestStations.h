#pragma once

#include "CoreMinimal.h"
#include "Station.h"
#include "StationTalkWidget.h"
#include "TestStations.generated.h"

UCLASS(NotBlueprintable, NotPlaceable, HideDropdown)
class UTestDerivedTalkWidget : public UStationTalkWidget
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

	virtual void ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete) override
	{
		++ProcessCallCount;
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

	virtual void ProcessBucket(ABucket* Bucket, FStationProcessComplete OnComplete) override
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
