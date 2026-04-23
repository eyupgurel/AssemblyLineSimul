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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Claude")
	int32 MaxTokens = 512;

	bool HasAPIKey() const { return !APIKey.IsEmpty(); }

	void SendMessage(const FString& Prompt, FClaudeComplete OnComplete);

private:
	FString APIKey;
	void LoadAPIKey();
};
