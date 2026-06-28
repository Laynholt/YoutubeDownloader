# Post-Processing Design

## Purpose

This document captures the target behavior for transcription and voice-over
translation after the prototype work in PR #1. The PR is treated as a prototype,
not as merge-ready architecture. Useful pieces may be reused, but the final
implementation should follow the behavior and plan below.

The existing download flow remains the primary product behavior. Transcription
and voice-over translation are optional manual actions on completed downloads.

## Product Behavior

### Download Flow

- Downloading videos continues to work as it does today.
- Download tasks that are not finished may be persisted in `download_queue.json`
  so they can resume after restart.
- Completed download tasks are not restored after restart.
- Transcription and voice-over state is never persisted in `download_queue.json`.
- Closing the application during transcription or voice-over cancels that
  post-processing work. It is not resumed on next launch.

### Completed Task Actions

Completed tasks with a known local media output show these actions:

- `Transcribe`
- `Translate`
- `Clear`

The task action is available for:

- a video that finished downloading in the current session;
- a video that already exists locally and becomes a completed task after the
  user pastes the same URL and starts the download again.

The action is based on the actual media output file found for the task. The
output filename scheme is not changed and quality is not added to filenames.
Different containers, such as `.mp4` and `.mkv`, remain different files.

If no media output path is known, post-processing actions are not runnable.

If a required tool is missing when the user clicks a completed-task action, do
not hide the action and do not show only a generic error. Show a custom
tool-readiness dialog with:

- what is not ready;
- what is missing;
- `Открыть Инструменты`;
- `Отмена`.

`Открыть Инструменты` opens the settings dialog directly on the `Инструменты`
section. It does not automatically start the original operation after settings
are saved. The user starts the operation again manually.

Readiness messages should distinguish at least:

- missing `whisper-cli.exe`;
- missing selected Whisper model;
- selected CUDA backend unavailable;
- missing `vot-helper.exe`.

### Settings Snapshot

Each post-processing operation uses an immutable snapshot of settings taken at
the moment the user clicks `Transcribe` or `Translate`.

If the user changes settings while an operation is already running, the running
operation continues with its original settings. New settings apply only to later
operations.

### Tool Readiness

Post-processing buttons remain visible when a completed task has a media file.
If the required tool is not ready, clicking the action opens a custom dialog with
the reason and actions such as:

- open settings;
- install now;
- cancel.

The application should not silently disable discoverability of transcription or
translation features.

### Global Download Folder Action

Do not add an `Open folder` button to each task. Add one global button near the
logs button. It opens the currently configured download directory from settings,
not a task-specific folder.

## Post-Processing Queue

Downloads and post-processing may run at the same time.

Post-processing has its own queue and limit. The first implementation uses one
active post-processing operation for the whole application.

If another valid post-processing action is requested while one is active, the
new operation is queued and the task shows a waiting state. It starts
automatically when the active operation finishes.

For a single task, only one post-processing operation may be active or waiting at
a time. While a task is being transcribed or translated:

- `Transcribe`, `Translate`, and `Clear` disappear or are disabled;
- the task shows progress or an indeterminate progress bar;
- the task shows `Cancel`.

For a waiting operation, `Cancel` removes it from the post-processing queue. For
an active operation, `Cancel` stops the external process, cleans temporary files
created by the current operation, leaves pre-existing result files untouched,
and returns the task to completed state.

Waiting post-processing operations are not persisted. Closing the application
stops the active operation and discards waiting operations.

Settings snapshots are captured at click time, before an operation is queued.
Queued operations keep that snapshot even if settings change before they start.

Overwrite/in-place confirmation is also shown at click time, before an operation
is queued. A queued operation should not open a modal dialog later when it
starts. At start time it revalidates the media path, tool paths, model paths,
and approved output paths. If a new conflict or missing dependency appears, the
operation fails gracefully, logs the reason, returns the task to completed
state, and lets the user retry manually after fixing the issue.

## Progress UI

Whisper may show determinate progress only if reliable progress can be parsed
from the executable output. Otherwise it uses indeterminate progress.

VOT voice-over uses indeterminate progress because the translation service does
not provide reliable progress. Show elapsed time. Do not invent ETA values.

After success, the task returns to completed state and shows a short result
status for the current session, such as generated filenames.

## Transcription

### Engines

The transcription engine is selected in settings:

- Whisper executable;
- VOT executable.

Both engines run through external `.exe` tools. The application does not embed a
library or API implementation for these engines.

### Outputs

Transcription always writes sidecar files next to the video:

- `.srt`
- `.txt`

The `.srt` file is required for subtitle embedding and burn-in. The `.txt` file
is a convenience transcript.

If either output already exists, show a custom overwrite confirmation listing
the affected files. If the user refuses, do not start the operation and keep the
task completed.

Use a shared overwrite dialog component for all affected-file confirmations:

- title;
- list of affected files;
- `Перезаписать`;
- `Отмена`.

For transcription, the affected list includes existing `.txt` and `.srt` files
that will be overwritten. If subtitle track or burn-in is enabled, also include
the original video file because it will be modified in place. Show this
confirmation even if the sidecar files do not exist yet.

### Subtitle Embedding

Subtitle embedding requires FFmpeg. The setting is mutually exclusive:

- off;
- add as subtitle track;
- burn into the video image.

Without FFmpeg, only `off` is available. The disabled FFmpeg-only options should
remain visible with a clear reason.

Embedding modifies the original downloaded video file. It must be implemented
with a temporary output file next to the original and an atomic replacement after
successful completion. The original must not be damaged by cancellation, FFmpeg
failure, application exit, or lack of disk space.

## Voice-Over Translation

### Tool

Voice-over translation uses `vot_exe`, installed as `vot-helper.exe`.

The application supports both:

- automatic installation into the application `tools` directory;
- choosing an existing folder or executable.

When the user chooses a folder, search only the selected folder and typical
nearby extracted subdirectories for the expected executable. If multiple matches
are found, ask the user to choose one.

### Outputs

Voice-over always creates a sidecar MP3 file next to the video. This is not a
setting. It is the baseline result and must work even when FFmpeg is unavailable.

Example output pattern:

- `Video1.vot.ru.mp3`

If the voice-over MP3 or any affected video output already exists, show a custom
overwrite confirmation. If the user refuses, do not start the operation and keep
the task completed.

For voice-over, the affected list includes the `.vot.<lang>.mp3` sidecar when it
will be overwritten. If audio-track or mix mode is enabled, also include the
original video file because it will be modified in place. Show this confirmation
even if the MP3 does not exist yet.

Do not track whether a video was previously modified by post-processing. If the
user runs another translation or transcription later, just apply the new
operation again after the normal affected-file confirmation.

### FFmpeg Video Integration

If FFmpeg is available, a mutually exclusive setting controls how the voice-over
MP3 is applied to the original video:

- off;
- add as an additional audio track;
- mix with the original audio track.

Without FFmpeg, only `off` is available. The other options remain visible but
disabled with a clear reason.

The FFmpeg step modifies the original downloaded video file, not a separate
`vot` or `mixed` video file. It must write to a temporary file first and replace
the original only after successful completion and basic output validation.

## Tool Installation

### VOT

Automatic installation downloads the latest `Laynholt/vot_exe` release zip,
verifies it against `SHA256SUMS.txt`, extracts it into the application tools
directory, configures the path to `vot-helper.exe`, and deletes the temporary
zip after success.

After installation or manual path selection, run a lightweight self-test that
does not require network access or create output files. Use a documented command
from the executable, such as `--version` or `--help`, after confirming what the
binary supports.

### Whisper

Automatic installation uses official `ggml-org/whisper.cpp` releases, not a
binary copied from PR #1.

Whisper executable installation and model download are separate actions:

- `Install Whisper` installs the executable/runtime.
- `Download model` downloads the selected model.

The default recommended model is `large-v3-turbo`. The UI should also offer
alternative models and show their sizes. Models are downloaded only after an
explicit user action.

### Whisper CPU and CUDA

CPU Whisper is always available as an install option.

CUDA Whisper may be offered when the application detects a likely NVIDIA/CUDA
candidate. CUDA readiness is not confirmed by GPU detection alone. The installed
CUDA backend is considered ready only after a successful self-test.

If CUDA self-test fails during setup or transcription, notify the user and switch
to CPU automatically when a working CPU installation and model are available.
This fallback is saved to settings. Settings should still expose a way to retry
CUDA verification or reinstall CUDA later.

## Settings UI

The final settings dialog uses the sidebar/card structure from PR #1 as a UI
shell, adapted to the current application style and final behavior. Do not keep
building on the temporary single-page settings layout.

Implement the new settings shell by rewriting the dialog against the current
branch, using PR #1 as a reference for layout patterns. Do not copy large chunks
of `DialogWindows.cpp` wholesale because the PR is coupled to rejected
configuration fields and behavior.

The sidebar sections are:

- `Загрузки`;
- `Транскрибация`;
- `Перевод`;
- `Инструменты`;
- `О программе`.

The sidebar collapsed state is runtime-only. It is not persisted in
`AppConfig`. The dialog may still auto-collapse by width.

### Settings Palette and Layout

Reuse the PR's sidebar, card, status badge, model dialog, and progress dialog
ideas, but align colors with the existing dark gray application palette. Avoid
the dark-blue bubble/accent style from the PR unless it is restyled to fit the
current UI.

### Tool Actions

Install, select, reinstall, and model-download actions live only in
`Инструменты`.

Workflow sections such as `Транскрибация` and `Перевод` show only workflow
settings. If a required tool is missing, the section shows a compact notice with
an `Открыть Инструменты` action. The install/select buttons are not duplicated
there.

The `Инструменты` section shows status cards for:

- `yt-dlp`;
- `FFmpeg`;
- `Whisper.cpp`;
- `Voice Over Translation` (`vot-helper.exe`).

`yt-dlp` may be read-only in the first pass, showing path/version/check status
without blocking the Whisper/VOT work.

The settings dialog API should support opening a specific initial section. The
normal settings button opens `Загрузки`; the tool-readiness dialog opens
`Инструменты`.

Suggested API shape:

- `ShowSettingsDialog(owner, instance, paths, config)`;
- `ShowSettingsDialog(owner, instance, paths, config, SettingsInitialSection::Tools)`.

### Workflow Availability

Controls that require missing tools remain visible but disabled. Disabled
controls are not clickable and explain the requirement through a tooltip.

FFmpeg-gated options:

- subtitle track;
- subtitle burn-in;
- voice-over audio track;
- voice-over mix.

These require FFmpeg. `Off` remains available.

Transcription engines are visible in `Транскрибация`:

- Whisper is available only when `whisper-cli.exe` and the selected model are
  ready.
- VOT is available only when `vot-helper.exe` is ready.
- If a previously selected engine is unavailable, show that clearly and point to
  `Инструменты`. Do not silently change the setting when merely opening the
  dialog.

### Language Controls

Use combo boxes, not free-text fields.

Voice-over target language options:

- `ru`;
- `en`;
- `de`;
- `es`;
- `it`;
- `pt`;
- `ja`;
- `ko`;
- `zh`.

Default voice-over language: `ru`. If config contains an unknown code, show and
save `ru`.

Transcription source language options:

- `auto`;
- `ru`;
- `en`;
- `de`;
- `es`;
- `it`;
- `pt`;
- `ja`;
- `ko`;
- `zh`.

Default transcription source language: `auto`. If config contains an unknown
code, show and save `auto`.

### Whisper Model UI

Whisper model selection uses a separate `Модели` dialog, as in PR #1. The
Whisper tool card shows the current model and whether it is downloaded.

The model dialog lists `WhisperManager::ModelCatalog()` entries. The
`large-v3-turbo` model is the recommended default. Downloading a model selects
it automatically.

### CUDA UI

Show the CUDA option even when CUDA is not available. If no NVIDIA/CUDA
candidate is detected, the CUDA action is disabled with a clear reason such as
`CUDA не обнаружена`.

If a candidate is detected, CUDA installation may be offered. After installation
run a self-test. If the self-test fails during setup or transcription, notify
the user and switch to CPU automatically when CPU plus model are ready. Persist
that fallback.

## Configuration

Persist tool and behavior settings, not operation state:

- selected transcription engine;
- Whisper executable path;
- Whisper model path and selected model;
- Whisper backend selection;
- VOT executable path;
- voice-over language;
- voice-over FFmpeg mode;
- subtitle FFmpeg mode;
- other existing download settings.

Post-processing operations are not persisted.

## Implementation Plan

Implementation should be staged. Do not combine the settings rewrite,
installer wiring, overwrite dialogs, and post-processing scheduler into one
large change.

Recommended stage order:

1. Replace the settings dialog with the sidebar/card shell.
2. Add tool cards and install/select/model actions in `Инструменты`.
3. Add completed-task readiness dialogs that open `Инструменты`.
4. Add the custom affected-file overwrite dialog.
5. Add the real post-processing waiting queue.
6. Add CUDA self-test/fallback polish.
7. Run manual UI smoke and targeted helper tests.

The next implementation stage should focus on the settings sidebar/card rewrite.
It should include:

- sidebar sections;
- card layout;
- tool status cards;
- select-folder actions for FFmpeg, Whisper, and VOT;
- the existing FFmpeg install flow;
- language combo boxes;
- workflow disabled states and tooltips;
- a Whisper model dialog shell if it can be ported cleanly.

The next settings stage does not need to finish:

- real Whisper install progress wiring;
- real VOT install progress wiring;
- CUDA self-test/fallback;
- custom overwrite dialogs;
- post-processing waiting queue.

### 1. Tool Readiness and Settings

- Add settings for transcription engine, Whisper paths/model/backend, VOT path,
  voice-over language, voice-over FFmpeg mode, and subtitle FFmpeg mode.
- Replace the temporary single-page settings UI with the sidebar/card settings
  shell described above.
- Rewrite the settings dialog by using PR #1 as a reference, not by copying the
  PR file wholesale.
- Keep tool-gated options visible but disabled when required tools are
  unavailable, with tooltip explanations.
- Implement tool status cards and install/select actions.
- Keep install/select/model actions in `Инструменты`; workflow sections should
  link to `Инструменты` instead of duplicating those actions.
- Add settings API support for opening directly on `Инструменты`.
- Implement VOT zip download, SHA256 verification, extraction, cleanup, and
  self-test.
- Implement Whisper official release installation separately from model download.
- Implement model catalog with `large-v3-turbo` as the recommended default.
- Implement CUDA detection, self-test, automatic CPU fallback, and persisted
  fallback.

### 2. Completed Task Actions and Queue

- Extend completed task UI with `Transcribe` and `Translate`.
- Require a known existing media output path before starting post-processing.
- Add a post-processing queue with a default active limit of one.
- Add waiting, running, completed, failed, and canceled presentation for
  post-processing without persisting it in `download_queue.json`.
- Add cancellation and cleanup of temporary files.
- Capture operation settings snapshots and overwrite approvals at click time,
  before queueing.
- Show tool-readiness dialogs with `Открыть Инструменты` for missing required
  tools.

### 3. Transcription

- Implement immutable settings snapshot for each transcription start.
- Implement Whisper transcription through the selected executable and model.
- Implement VOT transcription through `vot-helper.exe`.
- Always produce `.srt` and `.txt` sidecar files.
- Add overwrite confirmation for `.srt`, `.txt`, and any affected video file.
- Use the shared affected-file dialog and confirm in-place video modification
  even when sidecar files do not yet exist.
- Show determinate progress only when reliable; otherwise show indeterminate
  progress and elapsed time.

### 4. Voice-Over Translation

- Implement immutable settings snapshot for each translation start.
- Run `vot-helper.exe` to create the sidecar MP3.
- Always keep the MP3 sidecar result.
- Add overwrite confirmation for MP3 and any affected video file.
- Use the shared affected-file dialog and confirm in-place video modification
  even when the MP3 does not yet exist.
- Show indeterminate progress and elapsed time.

### 5. FFmpeg Integration

- Implement subtitle track embedding and subtitle burn-in.
- Implement voice-over audio track insertion and audio mixing.
- For every operation that modifies the original video, write a temporary file
  next to the original and replace the original only after success.
- Ensure cancellation and failures never corrupt the original video.

### 6. UI Polish and Verification

- Add one global `Open folder` button near logs that opens the current download
  directory.
- Keep task controls compact and avoid per-task folder buttons.
- Port the settings sidebar/card shell from PR #1, adapted to the final behavior
  and current palette.
- Remove the temporary settings controls during the sidebar rewrite instead of
  preserving both layouts.
- Add lightweight tests for pure settings helpers, such as language option
  normalization and tool-gated availability rules. Do not unit-test Win32 pixel
  layout.
- Add tests for queue behavior, settings snapshots, overwrite policy, tool
  readiness, fallback behavior, output path conflict handling, cancellation
  cleanup, and FFmpeg atomic replacement.
- Manually test completed task actions, queued post-processing, missing tools,
  VOT install, Whisper install/model download, CUDA fallback, and app close
  during post-processing.

## Current Branch Status

As of the current `codex/post-processing-foundation` work, the branch has the
post-processing foundation and settings sidebar/card shell in place.

Implemented or partially implemented:

- final config fields and app paths for Whisper, VOT, voice-over, and subtitles;
- VOT zip/checksum installation infrastructure plus lightweight `--version`
  self-test after install and manual path selection;
- Whisper release/model/backend infrastructure;
- manual completed-task actions for transcription and translation;
- separate post-processing queue with one active operation, waiting row state,
  cancellation for waiting/active operations, and non-persistent queued work;
- Whisper and VOT transcription to `.srt` and `.txt`;
- transcription now treats partial TXT/SRT sidecar commits as failure instead of
  reporting success with only one required output;
- VOT voice-over MP3 sidecar generation;
- FFmpeg subtitle/audio integration using temporary output and original video
  replacement;
- shared post-processing file commit helpers used by subtitle/audio FFmpeg
  integration and sidecar outputs, with tests for successful temp-file commit,
  failed media commit preserving the current original media, and failed sidecar
  commit preserving a pre-existing result file;
- global `Open folder` button near logs;
- custom tool-readiness dialogs that open `Инструменты`;
- affected-file confirmation listing sidecars and in-place video modification;
- tool readiness and FFmpeg-only mode fallback are resolved before affected-file
  confirmation, so the overwrite dialog reflects the effective operation;
- queued post-processing revalidates approved affected output paths at start and
  fails gracefully if a new output conflict appears after the original
  confirmation;
- sidebar/card settings dialog with collapsed sidebar icons, bottom `О
  программе`, custom language combo menus, and workflow tool notices;
- tool status cards with install/select/model actions centralized in
  `Инструменты`;
- VOT manual path selection supports direct `vot-helper.exe` selection and a
  candidate picker when a selected folder or nearby extracted subfolder contains
  multiple matches, without recursively scanning unrelated deep descendants;
- CUDA candidate detection for Whisper install selection and explicit readiness
  handling when selected CUDA falls back to CPU or runtime is unavailable;
- Whisper executable `--version` self-test after install/manual selection and
  persisted CPU fallback when selected CUDA fails readiness before transcription
  or fails during the transcription run while a tested CPU backend is available;
- persisted CPU fallback when CUDA Whisper installation reaches self-test but
  the CUDA executable fails and a tested CPU backend plus model are already
  available;
- Whisper tools UI shows CPU/CUDA availability and offers CPU/CUDA
  install/reinstall text based on the selected backend and CUDA candidate;
- FFmpeg-gated subtitle/audio integration controls stay visible and their
  tooltips explicitly explain that FFmpeg is required when the option is disabled;
- tests for many core path, config, tool, argument, and action helpers.

Verified in the current pass:

- Release build and core test suite;
- text audit for removed native combo boxes and old English mode labels;
- manual `PrintWindow` UI smoke of the main window, settings downloads,
  transcription, translation, tools, expanded tool details, and collapsed
  sidebar icons.
