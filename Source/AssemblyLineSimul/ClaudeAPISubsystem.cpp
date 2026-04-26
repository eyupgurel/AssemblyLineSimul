#include "ClaudeAPISubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogClaudeAPI, Log, All);

void UClaudeAPISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadAPIKey();
}

void UClaudeAPISubsystem::LoadAPIKey()
{
	// Search in priority order:
	//   1. Sandboxed Saved/  — where the editor and a manually-overridden packaged build look.
	//   2. Build/Secrets/    — staged into the .app via DirectoriesToAlwaysStageAsNonUFS so
	//                          packaged builds get the key without a manual post-package copy.
	const TArray<FString> Candidates = {
		FPaths::ProjectSavedDir() / TEXT("AnthropicAPIKey.txt"),
		FPaths::ProjectDir() / TEXT("Build/Secrets/AnthropicAPIKey.txt"),
	};

	for (const FString& Path : Candidates)
	{
		FString Raw;
		if (FFileHelper::LoadFileToString(Raw, *Path))
		{
			APIKey = Raw.TrimStartAndEnd();
			UE_LOG(LogClaudeAPI, Log, TEXT("Loaded Anthropic API key from %s (len=%d)"),
				*Path, APIKey.Len());
			return;
		}
	}

	UE_LOG(LogClaudeAPI, Warning,
		TEXT("No Anthropic API key found in any candidate location — Checker station will fall back to local verification."));
}

void UClaudeAPISubsystem::SendMessage(const FString& Prompt, FClaudeComplete OnComplete)
{
	if (APIKey.IsEmpty())
	{
		OnComplete.ExecuteIfBound(false, TEXT("No API key"));
		return;
	}

	const TSharedRef<IHttpRequest> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(TEXT("https://api.anthropic.com/v1/messages"));
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("x-api-key"), APIKey);
	Req->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
	Req->SetHeader(TEXT("content-type"), TEXT("application/json"));
	Req->SetTimeout(20.f);

	const TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), Model);
	Body->SetNumberField(TEXT("max_tokens"), MaxTokens);

	const TSharedPtr<FJsonObject> Msg = MakeShared<FJsonObject>();
	Msg->SetStringField(TEXT("role"), TEXT("user"));
	Msg->SetStringField(TEXT("content"), Prompt);

	TArray<TSharedPtr<FJsonValue>> Messages;
	Messages.Add(MakeShared<FJsonValueObject>(Msg));
	Body->SetArrayField(TEXT("messages"), Messages);

	FString JsonStr;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);
	Req->SetContentAsString(JsonStr);

	Req->OnProcessRequestComplete().BindLambda(
		[OnComplete](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
		{
			if (!bOk || !Response.IsValid())
			{
				UE_LOG(LogClaudeAPI, Warning, TEXT("HTTP request failed."));
				OnComplete.ExecuteIfBound(false, TEXT("HTTP failure"));
				return;
			}
			const int32 Code = Response->GetResponseCode();
			const FString Body = Response->GetContentAsString();
			if (Code < 200 || Code >= 300)
			{
				UE_LOG(LogClaudeAPI, Warning, TEXT("Claude API error %d: %s"), Code, *Body);
				OnComplete.ExecuteIfBound(false, Body);
				return;
			}

			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			{
				OnComplete.ExecuteIfBound(false, TEXT("Bad JSON"));
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
			if (!Root->TryGetArrayField(TEXT("content"), Content) || !Content || Content->Num() == 0)
			{
				OnComplete.ExecuteIfBound(false, TEXT("No content array"));
				return;
			}
			const TSharedPtr<FJsonObject>* Block = nullptr;
			if (!(*Content)[0]->TryGetObject(Block) || !Block || !Block->IsValid())
			{
				OnComplete.ExecuteIfBound(false, TEXT("No content block"));
				return;
			}
			FString Text;
			if (!(*Block)->TryGetStringField(TEXT("text"), Text))
			{
				OnComplete.ExecuteIfBound(false, TEXT("No text field"));
				return;
			}
			OnComplete.ExecuteIfBound(true, Text);
		});

	Req->ProcessRequest();
}
