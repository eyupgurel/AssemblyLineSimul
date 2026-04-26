#include "MacAudioCapture.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"

DEFINE_LOG_CATEGORY_STATIC(LogMacAudio, Log, All);

#if PLATFORM_MAC

#import <AVFoundation/AVFoundation.h>

namespace
{
	// UE Mac builds compile without ARC, so manage retain/release manually.
	AVAudioRecorder* AsRecorder(void* Handle)
	{
		return (AVAudioRecorder*)Handle;
	}

	void* StoreHandle(AVAudioRecorder* Rec)
	{
		// alloc/init returned a +1 retained Rec; just hand the pointer back.
		return (void*)Rec;
	}

	void ReleaseHandle(void*& Handle)
	{
		if (!Handle) return;
		AVAudioRecorder* Rec = (AVAudioRecorder*)Handle;
		[Rec release];
		Handle = nullptr;
	}
}

bool UMacAudioCapture::BeginRecord()
{
	if (RecorderHandle)
	{
		UE_LOG(LogMacAudio, Warning, TEXT("BeginRecord called while already recording — ignored."));
		return false;
	}

	// Pick a temp file path under <ProjectSaved>/VoiceCapture/<guid>.m4a
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("VoiceCapture");
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.DirectoryExists(*Dir))
	{
		PF.CreateDirectoryTree(*Dir);
	}
	CurrentFilePath = Dir / FString::Printf(TEXT("%s.m4a"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));

	NSURL* URL = [NSURL fileURLWithPath:CurrentFilePath.GetNSString()];
	NSDictionary* Settings = @{
		AVFormatIDKey:           @(kAudioFormatMPEG4AAC),
		AVSampleRateKey:         @16000.0,
		AVNumberOfChannelsKey:   @1,
		AVEncoderAudioQualityKey:@(AVAudioQualityMedium),
	};

	NSError* Err = nil;
	AVAudioRecorder* Rec = [[AVAudioRecorder alloc] initWithURL:URL settings:Settings error:&Err];
	if (!Rec || Err)
	{
		NSString* Desc = Err ? [Err localizedDescription] : @"unknown";
		UE_LOG(LogMacAudio, Warning, TEXT("AVAudioRecorder init failed: %s"), *FString(Desc));
		[Rec release];
		return false;
	}
	if (![Rec prepareToRecord])
	{
		UE_LOG(LogMacAudio, Warning, TEXT("prepareToRecord returned NO."));
		[Rec release];
		return false;
	}
	if (![Rec record])
	{
		UE_LOG(LogMacAudio, Warning, TEXT("record returned NO (mic permission denied?)."));
		[Rec release];
		return false;
	}

	RecorderHandle = StoreHandle(Rec);
	UE_LOG(LogMacAudio, Log, TEXT("Recording started → %s"), *CurrentFilePath);
	return true;
}

bool UMacAudioCapture::EndRecord(TArray<uint8>& OutBytes, FString& OutMimeType, FString& OutFilenameHint)
{
	if (!RecorderHandle)
	{
		UE_LOG(LogMacAudio, Warning, TEXT("EndRecord called when not recording."));
		return false;
	}

	AVAudioRecorder* Rec = AsRecorder(RecorderHandle);
	[Rec stop];
	ReleaseHandle(RecorderHandle);

	const FString Path = CurrentFilePath;
	CurrentFilePath.Reset();

	if (!FFileHelper::LoadFileToArray(OutBytes, *Path))
	{
		UE_LOG(LogMacAudio, Warning, TEXT("Failed to read captured audio file %s"), *Path);
		return false;
	}
	OutMimeType = TEXT("audio/m4a");
	OutFilenameHint = TEXT("audio.m4a");

	// Cleanup temp file — it's already in OutBytes.
	FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Path);
	UE_LOG(LogMacAudio, Log, TEXT("Captured %d bytes of audio."), OutBytes.Num());
	return true;
}

bool UMacAudioCapture::IsRecording() const
{
	return RecorderHandle != nullptr;
}

#else  // !PLATFORM_MAC

bool UMacAudioCapture::BeginRecord()
{
	UE_LOG(LogMacAudio, Warning, TEXT("Audio capture not implemented on this platform."));
	return false;
}

bool UMacAudioCapture::EndRecord(TArray<uint8>& OutBytes, FString& OutMimeType, FString& OutFilenameHint)
{
	OutBytes.Reset();
	OutMimeType.Reset();
	OutFilenameHint.Reset();
	return false;
}

bool UMacAudioCapture::IsRecording() const
{
	return false;
}

#endif
