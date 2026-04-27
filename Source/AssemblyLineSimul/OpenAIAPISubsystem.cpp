#include "OpenAIAPISubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogOpenAI, Log, All);

namespace
{
	// Append a UTF-8 encoded ASCII string to a byte buffer — used for the multipart
	// part headers / boundaries.
	void AppendAscii(TArray<uint8>& Out, const FString& Text)
	{
		const FTCHARToUTF8 Conv(*Text);
		Out.Append(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length());
	}
}

void UOpenAIAPISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadAPIKey();
}

void UOpenAIAPISubsystem::LoadAPIKey()
{
	const TArray<FString> Candidates = {
		FPaths::ProjectSavedDir()   / TEXT("OpenAIAPIKey.txt"),
		FPaths::ProjectContentDir() / TEXT("Secrets/OpenAIAPIKey.txt"),
	};

	for (const FString& Path : Candidates)
	{
		FString Raw;
		if (FFileHelper::LoadFileToString(Raw, *Path))
		{
			APIKey = Raw.TrimStartAndEnd();
			UE_LOG(LogOpenAI, Log, TEXT("Loaded OpenAI API key from %s (len=%d)"),
				*Path, APIKey.Len());
			return;
		}
	}

	UE_LOG(LogOpenAI, Warning,
		TEXT("No OpenAI API key found in any candidate location — voice transcription disabled."));
}

TArray<uint8> UOpenAIAPISubsystem::BuildWhisperMultipartBody(const FString& Boundary,
                                                              const FString& InModel,
                                                              const FString& Language,
                                                              const FString& MimeType,
                                                              const FString& FilenameHint,
                                                              const TArray<uint8>& AudioBytes)
{
	const FString CRLF = TEXT("\r\n");
	TArray<uint8> Body;
	Body.Reserve(AudioBytes.Num() + 1024);

	// --- model field ---
	AppendAscii(Body, FString::Printf(TEXT("--%s%s"), *Boundary, *CRLF));
	AppendAscii(Body, FString::Printf(TEXT("Content-Disposition: form-data; name=\"model\"%s%s"), *CRLF, *CRLF));
	AppendAscii(Body, FString::Printf(TEXT("%s%s"), *InModel, *CRLF));

	// --- response_format field ---
	AppendAscii(Body, FString::Printf(TEXT("--%s%s"), *Boundary, *CRLF));
	AppendAscii(Body, FString::Printf(TEXT("Content-Disposition: form-data; name=\"response_format\"%s%s"), *CRLF, *CRLF));
	AppendAscii(Body, FString::Printf(TEXT("json%s"), *CRLF));

	// --- language field (force language so Whisper doesn't auto-detect locale) ---
	AppendAscii(Body, FString::Printf(TEXT("--%s%s"), *Boundary, *CRLF));
	AppendAscii(Body, FString::Printf(TEXT("Content-Disposition: form-data; name=\"language\"%s%s"), *CRLF, *CRLF));
	AppendAscii(Body, FString::Printf(TEXT("%s%s"), *Language, *CRLF));

	// --- file field (binary) ---
	const FString SafeFilename = FilenameHint.IsEmpty() ? FString(TEXT("audio.wav")) : FilenameHint;
	const FString SafeMime     = MimeType.IsEmpty()     ? FString(TEXT("audio/wav")) : MimeType;
	AppendAscii(Body, FString::Printf(TEXT("--%s%s"), *Boundary, *CRLF));
	AppendAscii(Body, FString::Printf(TEXT("Content-Disposition: form-data; name=\"file\"; filename=\"%s\"%s"),
		*SafeFilename, *CRLF));
	AppendAscii(Body, FString::Printf(TEXT("Content-Type: %s%s%s"), *SafeMime, *CRLF, *CRLF));
	Body.Append(AudioBytes);
	AppendAscii(Body, CRLF);

	// --- closing boundary ---
	AppendAscii(Body, FString::Printf(TEXT("--%s--%s"), *Boundary, *CRLF));

	return Body;
}

void UOpenAIAPISubsystem::TranscribeAudio(const TArray<uint8>& AudioBytes,
                                          const FString& MimeType,
                                          const FString& FilenameHint,
                                          FWhisperComplete OnComplete)
{
	if (APIKey.IsEmpty())
	{
		OnComplete.ExecuteIfBound(false, TEXT("No API key"));
		return;
	}
	if (AudioBytes.Num() == 0)
	{
		OnComplete.ExecuteIfBound(false, TEXT("Empty audio buffer"));
		return;
	}

	const FString Boundary = FString::Printf(TEXT("----AssemblyLineBoundary%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const TArray<uint8> Body = BuildWhisperMultipartBody(
		Boundary, Model, /*Language=*/TEXT("en"), MimeType, FilenameHint, AudioBytes);

	const TSharedRef<IHttpRequest> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(TEXT("https://api.openai.com/v1/audio/transcriptions"));
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *APIKey));
	Req->SetHeader(TEXT("Content-Type"),
		FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
	Req->SetTimeout(30.f);
	Req->SetContent(Body);

	Req->OnProcessRequestComplete().BindLambda(
		[OnComplete](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
		{
			if (!bOk || !Response.IsValid())
			{
				UE_LOG(LogOpenAI, Warning, TEXT("Whisper HTTP request failed."));
				OnComplete.ExecuteIfBound(false, TEXT("HTTP failure"));
				return;
			}
			const int32 Code = Response->GetResponseCode();
			const FString BodyStr = Response->GetContentAsString();
			if (Code < 200 || Code >= 300)
			{
				UE_LOG(LogOpenAI, Warning, TEXT("Whisper API error %d: %s"), Code, *BodyStr);
				OnComplete.ExecuteIfBound(false, BodyStr);
				return;
			}

			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyStr);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			{
				OnComplete.ExecuteIfBound(false, TEXT("Bad JSON"));
				return;
			}
			FString Text;
			if (!Root->TryGetStringField(TEXT("text"), Text))
			{
				OnComplete.ExecuteIfBound(false, TEXT("No text field"));
				return;
			}
			OnComplete.ExecuteIfBound(true, Text.TrimStartAndEnd());
		});

	Req->ProcessRequest();
}
