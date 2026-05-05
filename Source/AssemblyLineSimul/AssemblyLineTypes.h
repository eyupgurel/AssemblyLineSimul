#pragma once

#include "CoreMinimal.h"
#include "AssemblyLineTypes.generated.h"

UENUM(BlueprintType)
enum class EStationType : uint8
{
	Generator    UMETA(DisplayName = "Generator"),
	Filter       UMETA(DisplayName = "Filter (Primes)"),
	Sorter       UMETA(DisplayName = "Sorter"),
	Checker      UMETA(DisplayName = "Checker (LLM)"),
	// Story 32a — chat-only meta agent. Receives the operator's mission at boot
	// and emits a DAG spec (parsed by OrchestratorParser) describing the line
	// to spawn. Never appears in a station's processing chain — its
	// ProcessBucket is unreachable.
	Orchestrator UMETA(DisplayName = "Orchestrator")
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

class APayloadCarrier;
DECLARE_DELEGATE_OneParam(FStationProcessComplete, FStationProcessResult);
