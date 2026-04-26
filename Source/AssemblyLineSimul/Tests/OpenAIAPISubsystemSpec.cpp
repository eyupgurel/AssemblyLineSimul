#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OpenAIAPISubsystem.h"

DEFINE_SPEC(FOpenAIAPISubsystemSpec,
	"AssemblyLineSimul.OpenAIAPISubsystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace
{
	// Decode a multipart-form-data byte buffer back to UTF-8 string for substring assertions.
	// (The body is overwhelmingly ASCII; the embedded binary audio chunk is just bytes that
	// roundtrip fine for substring search of the surrounding form fields.)
	FString DecodeBody(const TArray<uint8>& Body)
	{
		FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Body.GetData()), Body.Num());
		return FString(Conv.Length(), Conv.Get());
	}
}

void FOpenAIAPISubsystemSpec::Define()
{
	Describe("BuildWhisperMultipartBody", [this]()
	{
		It("pins language=en so Whisper does not auto-detect to the speaker's locale", [this]()
		{
			const TArray<uint8> FakeAudio = { 0x00, 0x11, 0x22, 0x33, 0x44 };
			const TArray<uint8> Body = UOpenAIAPISubsystem::BuildWhisperMultipartBody(
				TEXT("BOUNDARY-TEST"), TEXT("whisper-1"), TEXT("en"),
				TEXT("audio/m4a"), TEXT("test.m4a"), FakeAudio);

			const FString Decoded = DecodeBody(Body);
			TestTrue(TEXT("contains language form-field"),
				Decoded.Contains(TEXT("name=\"language\"")));
			TestTrue(TEXT("language value is 'en'"),
				Decoded.Contains(TEXT("\r\nen\r\n")));
		});

		It("includes the model, response_format, and file form fields", [this]()
		{
			const TArray<uint8> FakeAudio = { 0xAA, 0xBB };
			const TArray<uint8> Body = UOpenAIAPISubsystem::BuildWhisperMultipartBody(
				TEXT("BOUNDARY-TEST"), TEXT("whisper-1"), TEXT("en"),
				TEXT("audio/m4a"), TEXT("clip.m4a"), FakeAudio);
			const FString Decoded = DecodeBody(Body);

			TestTrue(TEXT("model field present"),
				Decoded.Contains(TEXT("name=\"model\"")));
			TestTrue(TEXT("model value is whisper-1"),
				Decoded.Contains(TEXT("\r\nwhisper-1\r\n")));
			TestTrue(TEXT("response_format field present"),
				Decoded.Contains(TEXT("name=\"response_format\"")));
			TestTrue(TEXT("file field present with filename"),
				Decoded.Contains(TEXT("name=\"file\"; filename=\"clip.m4a\"")));
			TestTrue(TEXT("audio Content-Type header present"),
				Decoded.Contains(TEXT("Content-Type: audio/m4a")));
			TestTrue(TEXT("body ends with closing boundary"),
				Decoded.Contains(TEXT("--BOUNDARY-TEST--")));
		});

		It("inlines the raw audio bytes verbatim into the file part", [this]()
		{
			const TArray<uint8> FakeAudio = { 0xDE, 0xAD, 0xBE, 0xEF };
			const TArray<uint8> Body = UOpenAIAPISubsystem::BuildWhisperMultipartBody(
				TEXT("BOUNDARY-X"), TEXT("whisper-1"), TEXT("en"),
				TEXT("audio/m4a"), TEXT("a.m4a"), FakeAudio);

			// The 4 audio bytes appear as a contiguous subsequence somewhere in the body.
			bool bFound = false;
			for (int32 i = 0; i + FakeAudio.Num() <= Body.Num(); ++i)
			{
				if (FMemory::Memcmp(&Body[i], FakeAudio.GetData(), FakeAudio.Num()) == 0)
				{
					bFound = true; break;
				}
			}
			TestTrue(TEXT("raw audio bytes embedded verbatim"), bFound);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
