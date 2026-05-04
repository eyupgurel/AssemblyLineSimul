#!/usr/bin/env bash
# Patches the staged Mac .app so voice push-to-talk works in packaged builds.
#
# Why this exists:
#   UE's +ExtraPlistData=<key>NSMicrophoneUsageDescription</key>... in
#   Config/DefaultEngine.ini *should* inject the mic-usage string into the
#   packaged Info.plist, but UAT silently drops it in UE 5.7 (worked in
#   earlier 5.x). Without that key macOS denies mic access without ever
#   prompting; AVAudioRecorder records 3 s of silence; Whisper transcribes
#   silence as "you". This script forces the fix.
#
#   The Info.plist edit invalidates the existing code signature, so the
#   .app is also re-signed ad-hoc; otherwise macOS refuses to launch it.
#
# Usage (run after `RunUAT BuildCookRun` Mac package completes):
#   ./Build/Mac/Scripts/fix_voice_in_packaged_app.sh
#   ./Build/Mac/Scripts/fix_voice_in_packaged_app.sh /path/to/MyApp.app   # explicit
#
# Optional: pass --reset-permission to also `tccutil reset Microphone` for
# the bundle id, forcing macOS to re-prompt on next launch.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
DEFAULT_APP="$PROJECT_ROOT/Saved/StagedBuilds/Mac/AssemblyLineSimul.app"

# Pick APP_PATH from the first POSITIONAL arg (one that doesn't start
# with --). Without this, `./fix.sh --reset-permission` would mistakenly
# treat `--reset-permission` as the app path and bail out.
APP_PATH="$DEFAULT_APP"
RESET_PERMISSION=0
for arg in "$@"; do
    case "$arg" in
        --reset-permission) RESET_PERMISSION=1 ;;
        --*)                echo "Warning: unknown flag '$arg'" >&2 ;;
        *)                  APP_PATH="$arg" ;;
    esac
done

if [[ ! -d "$APP_PATH" ]]; then
    echo "Error: app not found at $APP_PATH" >&2
    exit 1
fi

PLIST="$APP_PATH/Contents/Info.plist"
USAGE_STRING="Required for voice push-to-talk to converse with the AI agents."

if /usr/libexec/PlistBuddy -c "Print :NSMicrophoneUsageDescription" "$PLIST" >/dev/null 2>&1; then
    /usr/libexec/PlistBuddy -c "Set :NSMicrophoneUsageDescription '$USAGE_STRING'" "$PLIST"
    echo "Updated NSMicrophoneUsageDescription"
else
    /usr/libexec/PlistBuddy -c "Add :NSMicrophoneUsageDescription string '$USAGE_STRING'" "$PLIST"
    echo "Added NSMicrophoneUsageDescription"
fi

codesign --force --deep --sign - "$APP_PATH" 2>&1 | tail -1
codesign --verify --verbose=2 "$APP_PATH" 2>&1 | tail -2

if [[ "$RESET_PERMISSION" == "1" ]]; then
    BUNDLE_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$PLIST")"
    tccutil reset Microphone "$BUNDLE_ID" >/dev/null 2>&1 \
        && echo "Reset Microphone permission for $BUNDLE_ID"
fi

echo "Done. App ready at: $APP_PATH"
