#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "ClaudeAPISubsystem.generated.h"

DECLARE_DELEGATE_TwoParams(FClaudeComplete, bool /*bSuccess*/, const FString& /*Response*/);

UCLASS()
class ASSEMBLYLINESIMUL_API UClaudeAPISubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Claude")
	FString Model = TEXT("claude-sonnet-4-6");

	// Story 33b raised this from 512 to 4096. The Orchestrator's reply
	// (DAG spec + per-agent Role prose) routinely exceeds 512 tokens —
	// truncation results in a malformed JSON object that ParsePlan can't
	// recover from, silently dropping the spawn. 4096 leaves headroom
	// for ~4 stations with thoughtful Role paragraphs each.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Claude")
	int32 MaxTokens = 4096;

	bool HasAPIKey() const { return !APIKey.IsEmpty(); }

	void SendMessage(const FString& Prompt, FClaudeComplete OnComplete);

private:
	FString APIKey;
	void LoadAPIKey();
};
