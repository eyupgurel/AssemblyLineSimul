#pragma once

#include "CoreMinimal.h"
#include "AssemblyLineTypes.generated.h"

UENUM(BlueprintType)
enum class EStationType : uint8
{
	Generator UMETA(DisplayName = "Generator"),
	Filter    UMETA(DisplayName = "Filter (Primes)"),
	Sorter    UMETA(DisplayName = "Sorter"),
	Checker   UMETA(DisplayName = "Checker (LLM)")
};

USTRUCT(BlueprintType)
struct FStationProcessResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bAccepted = true;

	UPROPERTY(BlueprintReadOnly)
	FString Reason;

	UPROPERTY(BlueprintReadOnly)
	EStationType SendBackTo = EStationType::Filter;
};

USTRUCT(BlueprintType)
struct FAgentChatMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString Role;  // "user" or "assistant"

	UPROPERTY(BlueprintReadOnly)
	FString Text;
};

class ABucket;
DECLARE_DELEGATE_OneParam(FStationProcessComplete, FStationProcessResult);
