#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "OpenAIAPISubsystem.generated.h"

DECLARE_DELEGATE_TwoParams(FWhisperComplete, bool /*bSuccess*/, const FString& /*Transcript*/);

UCLASS()
class ASSEMBLYLINESIMUL_API UOpenAIAPISubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenAI")
	FString Model = TEXT("whisper-1");

	bool HasAPIKey() const { return !APIKey.IsEmpty(); }

	// Posts audio bytes to OpenAI's /v1/audio/transcriptions endpoint as multipart/form-data.
	// MimeType examples: "audio/wav", "audio/mp4", "audio/mpeg". Filename hint is encoded in
	// the Content-Disposition so Whisper can guess format if MimeType is missing.
	void TranscribeAudio(const TArray<uint8>& AudioBytes,
	                     const FString& MimeType,
	                     const FString& FilenameHint,
	                     FWhisperComplete OnComplete);

private:
	FString APIKey;
	void LoadAPIKey();
};
