# UI Behavior Fixes Design

## Scope

This change delivers four focused corrections to the existing Win32 application:

- remove the unused `ffmpegPromptDismissed` configuration field;
- prevent the main or parent window from flashing when a modal child closes;
- show a modal explanation when download is attempted while yt-dlp is being installed or updated;
- make both the Paste button and `Ctrl+V` replace the complete URL field contents.

No layout redesign, configuration migration, or yt-dlp installation workflow redesign is included.

## Configuration cleanup

`ffmpegPromptDismissed` will be removed from `AppConfig`, configuration loading and saving, the FFmpeg dialog skip handler, and configuration round-trip tests. Existing `config.ini` files may still contain `ffmpeg_prompt_dismissed`; the JSON loader already ignores unknown keys, so no migration is required. The next configuration save will omit the obsolete key.

The FFmpeg Skip button will continue to close the dialog. It will no longer report a configuration change merely to persist the removed flag.

## Modal-window restoration

`RunModal` will record whether its owner was enabled before opening the child. When the child closes, it will restore that enabled state and activate the owner without calling `BringWindowToTop`.

This follows normal modal ownership behavior while avoiding the explicit Z-order operation that currently forces the owner to repaint visibly. Nested dialogs must not accidentally enable an owner that was already disabled by an outer modal operation.

## Download attempt while yt-dlp is unavailable

The Download button will remain clickable while the startup yt-dlp check, installation, or update is running. `EnqueueCurrentUrl` remains the single guard before queue insertion.

If yt-dlp is not ready, the application will show the existing custom modal error dialog with an OK button. Its message will tell the user to wait until yt-dlp installation or updating is complete. No queue item will be created and URL validation will not run until yt-dlp is ready.

After a successful tool check, download behavior is unchanged. If preparation fails and yt-dlp remains unavailable, the same guard prevents an invalid download attempt and explains that yt-dlp is not ready.

## URL paste behavior

Both supported paste paths will use one shared operation:

1. focus the URL edit control;
2. select its complete contents with `EM_SETSEL`;
3. send `WM_PASTE`.

This applies to the Paste button and the application-level `Ctrl+V` shortcut. Pasting replaces an existing URL instead of inserting at the cursor. The edit control's normal `EN_CHANGE` notification remains responsible for starting preview refresh, avoiding a duplicate explicit refresh call.

## Testing

Automated coverage will verify:

- loading an old configuration containing `ffmpeg_prompt_dismissed` succeeds, and saving it removes that key;
- the shared paste action selects the entire edit-control value before paste;
- the download-attempt policy rejects attempts until yt-dlp is ready;
- modal owner restoration preserves the owner's prior enabled state.

The final verification will build Debug and Release configurations, run the full test suite, and manually exercise the native window flows for modal close, guarded download, Paste button, and `Ctrl+V`.
