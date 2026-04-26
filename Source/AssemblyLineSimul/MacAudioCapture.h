#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MacAudioCapture.generated.h"

// Thin wrapper around macOS AVAudioRecorder. Records mic audio to an M4A file
// (AAC) — Whisper accepts the format directly. Implementation lives in
// MacAudioCapture.mm and is compiled only on Mac; non-Mac builds get stubs that
// return false so the rest of the voice pipeline degrades gracefully.
UCLASS()
class ASSEMBLYLINESIMUL_API UMacAudioCapture : public UObject
{
	GENERATED_BODY()

public:
	// Start recording to a temporary file. Returns true on success. The first
	// call on a new app run may trigger a macOS microphone-permission prompt.
	bool BeginRecord();

	// Stop recording and read the captured file's bytes into OutBytes. Returns
	// true on success. The on-disk file is deleted after read.
	bool EndRecord(TArray<uint8>& OutBytes, FString& OutMimeType, FString& OutFilenameHint);

	bool IsRecording() const;

private:
	// Opaque pointer to the underlying AVAudioRecorder* (kept as void* so this
	// header stays Obj-C-free). Allocated/freed inside MacAudioCapture.mm.
	void* RecorderHandle = nullptr;

	FString CurrentFilePath;
};
