# PR #1 Triage

Date: 2026-06-26

PR: https://github.com/Laynholt/YoutubeDownloader/pull/1

Base inspected: `master`

Head inspected: `origin/pr/1` at `feef73e`

## Summary

PR #1 is a prototype. It should not be merged as-is.

The useful work is mostly in isolated helpers, parsers, tests, and installer
prototypes. The rejected work is the merged architecture: automatic
transcription after download, post-processing state inside the normal download
request/store, npm-based `vot-cli`, separate generated voice-over video files,
and the large settings UX refresh coupled to functional changes.

The target implementation should follow `docs/post-processing-design.md`.

## Keep or Adapt

### `src/Core/AppPaths.*`

Status: adapt.

Useful:

- local Whisper directory paths;
- CPU/CUDA install directories;
- model directory;
- temp directories for transcription and voice-over.

Changes needed:

- add a local VOT directory and `vot-helper.exe` path for `Laynholt/vot_exe`;
- keep paths as infrastructure only, not as a reason to put post-processing into
  download requests.

### `src/Core/BackendText.*`

Status: keep.

Useful:

- `FormatElapsedDuration`;
- Russian plural handling for elapsed time.

This supports indeterminate post-processing progress with elapsed time.

### `src/Core/Config.*`

Status: adapt.

Useful:

- Whisper backend enum and config serialization helpers;
- Whisper executable/model paths;
- voice-over language config shape.

Changes needed:

- remove `transcribeAfterDownload`;
- replace `voiceOverMode` with the final FFmpeg voice-over integration mode:
  `off`, `audio_track`, `mix`;
- add subtitle integration mode: `off`, `subtitle_track`, `burn_in`;
- replace `votCliPath` with `votExePath` or equivalent naming for
  `vot-helper.exe`;
- add selected transcription engine: `whisper` or `vot`;
- preserve "settings only" data, never active post-processing operation state.

### `src/Core/YtDlpClient.*`

Status: mostly keep/adapt.

Useful:

- `ExtractYtDlpOutputPath`;
- parsing "has already been downloaded";
- `SnapshotOutputDirectory`;
- `FindDownloadedMediaFile`;
- `ParseYtDlpProcessLine`;
- progress parsing improvements that reset correctly between video/audio tracks.

Changes needed:

- remove Whisper/VOT fields from `YtDlpDownloadRequest`;
- keep `YtDlpDownloadRequest` scoped to download-only data;
- keep output path detection so completed tasks can expose manual
  post-processing actions.

### `src/Core/ToolManagers.*`

Status: split and adapt.

Useful:

- Whisper model catalog shape;
- `large-v3-turbo` recommended model;
- official `ggml-org/whisper.cpp` release lookup;
- CPU/CUDA install directory resolution;
- Whisper model download flow;
- executable discovery in extracted archives;
- progress callback pattern reused from FFmpeg installation.

Changes needed:

- verify current Whisper release asset names before hardcoding;
- add SHA/checksum verification where available;
- add CUDA self-test and persisted fallback to CPU;
- replace `VotCliManager` entirely with a `VotExeManager`;
- VOT installation must download the latest `Laynholt/vot_exe` zip, verify it
  using `SHA256SUMS.txt`, extract `vot-helper.exe`, delete the temporary zip,
  and run a lightweight self-test;
- remove npm/Node assumptions.

### `src/Core/TranscriptionClient.*`

Status: rewrite around useful helpers.

Useful:

- Whisper command argument construction;
- `.txt` and `.srt` output expectation;
- Whisper progress parsing;
- last meaningful process error summary;
- temp output paths.

Problems:

- only supports Whisper, not VOT transcription;
- requires FFmpeg before transcription starts;
- extracts WAV as a mandatory step, which may be valid for Whisper but should be
  treated as an engine implementation detail;
- no overwrite confirmation policy;
- no subtitle embedding modes;
- no operation snapshot type independent from download request.

Target:

- introduce a transcription operation layer with selected engine
  `whisper`/`vot`;
- always produce `.srt` and `.txt`;
- use overwrite confirmation before starting;
- optionally embed subtitle track or burn in through FFmpeg using temp output
  and atomic replacement.

### `src/Core/VoiceOverTranslationClient.*`

Status: mostly rewrite.

Useful:

- language-safe filename helper;
- MP3 sidecar naming idea: `.vot.<lang>.mp3`;
- FFmpeg argument prototypes for adding an audio track and mixing audio;
- process error summary pattern.

Problems:

- targets `vot-cli`, not `vot-helper.exe`;
- requires FFmpeg even though baseline voice-over should still produce MP3
  without FFmpeg;
- produces separate video files such as `.vot.ru.mp4` or `.vot-mixed.ru.mkv`;
- removes/replaces output paths before the final operation succeeds;
- does not implement atomic replacement of the original video.

Target:

- always create sidecar MP3;
- FFmpeg mode `off` leaves only MP3;
- FFmpeg modes modify the original video through a temp file and replace it only
  after success;
- no generated `.vot`/`.vot-mixed` video filenames.

### `src/UI/UiActions.*`

Status: partially adapt.

Useful:

- finding transcript result paths;
- simple status text helpers;
- edit menu changes remain unrelated but harmless if still needed.

Changes needed:

- update result discovery for final naming and modes;
- remove "voice-over video path" concept because final FFmpeg output is the
  original video file;
- add helpers for conflict detection if useful.

### `tests/test_core.cpp`

Status: mine heavily, rewrite expectations.

Useful tests to preserve conceptually:

- config round-trip for tool settings;
- unsupported language normalization;
- ytdlp output path parsing;
- output file fallback when yt-dlp reports an existing download;
- Whisper argument construction;
- Whisper progress parsing;
- process error summary;
- Whisper release/model catalog tests;
- Whisper backend resolution;
- executable discovery;
- post-processing state returning a task to completed;
- cancellation/cleanup concepts;
- elapsed time formatting.

Tests that need replacement:

- automatic `transcribeAfterDownload`;
- npm/vot-cli install invocation;
- voice-over generated video path resolution;
- post-processing persisted as a normal download state;
- any tests expecting `.vot-mixed.<lang>.<container>` video outputs.
- Kazakh language migration tests from the prototype.

## Rewrite or Discard

### `.codegraph/*`

Status: discard.

These are local CodeGraph runtime files and must not be committed.

### `docs/superpowers/specs/2026-06-25-ux-refresh-design.md`

Status: discard or keep outside this PR.

The UX refresh is not part of the first post-processing implementation. Some
ideas can inform a later UI redesign, but this file includes rejected behavior
such as automatic transcription after download.

### `src/UI/DialogWindows.cpp`

Status: adapt the settings shell, reject the old behavior.

Useful:

- sidebar settings shell;
- collapsible sidebar behavior as runtime UI state;
- section navigation;
- card layout;
- tool status cards;
- Whisper backend/model dialogs;
- progress dialog callback pattern;
- compact status badges.

Do not port:

- `Transcribe after download`;
- `vot-cli` or npm installation assumptions;
- old `voiceOverSeparate` / `voiceOverMixed` semantics;
- generated separate voice-over video outputs;
- persisted sidebar collapsed state;
- PR colors that do not fit the current dark gray app palette.

Target:

- replace the temporary single-page settings dialog with a sidebar/card dialog;
- rewrite the dialog in the current branch using PR #1 as a reference; do not
  copy large chunks of the PR file wholesale;
- use sections `Загрузки`, `Транскрибация`, `Перевод`, `Инструменты`,
  `О программе`;
- keep install/select/reinstall/model-download actions only in `Инструменты`;
- keep workflow sections focused on workflow settings, with disabled controls
  and an `Открыть Инструменты` notice when tools are missing;
- name the VOT tool card `Voice Over Translation` and show `vot-helper.exe` in
  details.
- support opening settings directly on `Инструменты` from tool-readiness
  dialogs.

### `src/UI/Application.*`

Status: rewrite behavior, salvage small helpers.

Problems:

- embeds post-processing execution inside the download executor;
- carries Whisper/VOT settings inside `YtDlpDownloadRequest`;
- supports automatic transcription after download;
- blocks VOT if FFmpeg is missing, even though MP3 sidecar should still work;
- hides repeat actions when a result exists instead of showing overwrite
  confirmation;
- treats voice-over result as a separate video file.

Reuse only:

- row button hit-testing concepts;
- completed task action placement;
- output path lookup helpers after they are moved to a better layer;
- `StartPostProcessing` call pattern after queue semantics are corrected.

### `src/Core/DownloadQueue.*`

Status: rewrite/adapt carefully.

Useful:

- a completed task can temporarily enter a post-processing UI state;
- status/progress callbacks can update the same task row;
- post-processing result can return the task to completed;
- cancellation can use the existing stop/cancel pattern.

Problems:

- `PostProcessing` is treated like a persisted running download state;
- post-processing shares `m_activeCount` and worker scheduling with downloads;
- no separate post-processing queue or active limit;
- no waiting state for queued post-processing;
- state naming is too tied to the download queue.

Target:

- keep download queue persistence unchanged;
- add a separate post-processing scheduler/queue with active limit `1`;
- for UI, a task may show `WaitingProcessing` or `Processing`, but these states
  must not be exported to `download_queue.json`;
- one task may have at most one active or waiting operation.

### `src/Core/DownloadQueueStore.cpp`

Status: mostly reject PR changes.

Do not persist Whisper/VOT paths, temp directories, or post-processing state in
task records. Persist only download task data needed to resume unfinished
downloads.

## File-by-File Decision Table

| File | Decision | Notes |
| --- | --- | --- |
| `.codegraph/.gitignore` | discard | Local CodeGraph metadata. |
| `.codegraph/daemon.pid` | discard | Runtime PID file. |
| `CMakeLists.txt` | adapt | Add new source files only after final names are known. |
| `docs/superpowers/specs/2026-06-25-ux-refresh-design.md` | discard/defer | Separate UX refresh, includes rejected behavior. |
| `src/Core/AppPaths.*` | adapt | Good Whisper paths; add VOT exe paths. |
| `src/Core/BackendText.*` | keep | Elapsed duration helper is useful. |
| `src/Core/Config.*` | adapt | Keep tool settings; remove auto-transcribe and update modes. |
| `src/Core/DownloadQueue.*` | rewrite/adapt | Need separate post-processing queue, no persisted processing. |
| `src/Core/DownloadQueueStore.cpp` | reject most | Do not persist post-processing/tool runtime fields in tasks. |
| `src/Core/ToolManagers.*` | split/adapt | Keep Whisper ideas; replace VOT npm manager. |
| `src/Core/TranscriptionClient.*` | rewrite around helpers | Needs VOT engine and final output policy. |
| `src/Core/VoiceOverTranslationClient.*` | rewrite mostly | Sidecar MP3 useful; video output behavior wrong. |
| `src/Core/YtDlpClient.*` | keep/adapt | Output path detection is important. |
| `src/UI/Application.*` | rewrite mostly | Manual actions good; architecture wrong. |
| `src/UI/DialogWindows.cpp` | adapt settings shell | Use sidebar/cards/dialog ideas; reject auto-transcribe, npm/vot-cli, old modes, persisted sidebar state. |
| `src/UI/UiActions.*` | adapt | Result path helpers need final naming. |
| `tests/test_core.cpp` | mine/rewrite | Many valuable tests but expectations must change. |

## Recommended Extraction Order

1. Port `BackendText` elapsed duration tests and helper.
2. Port `YtDlpClient` output path parsing and fallback media discovery.
3. Add final config fields and AppPaths for Whisper/VOT, without touching queue
   persistence.
4. Build final tool managers: Whisper first, then VOT exe zip/checksum install.
5. Implement separate post-processing queue and completed task actions.
6. Rebuild transcription and voice-over clients against the agreed output and
   overwrite policy.
7. Add FFmpeg atomic replacement for subtitle/audio integration.
8. Replace the temporary settings dialog with the PR-style sidebar/card shell,
   adapted to final sections and the current palette.
9. Wire `Инструменты` cards to FFmpeg, yt-dlp, Whisper, VOT status plus
   install/select/model actions.
10. Wire `Транскрибация` and `Перевод` workflow sections to final config
    fields, disabled states, combo-box language presets, and tooltips.
11. Replace generic missing-tool errors from completed-task actions with a
    custom readiness dialog that offers `Открыть Инструменты`.
12. Add the shared affected-file overwrite dialog and use it for sidecar
    overwrites and in-place video modification confirmation.
13. Add the real post-processing waiting queue: active limit one, no persistence,
    settings snapshot and overwrite approval at click time, graceful failure if
    tools or files disappear before a queued operation starts.

## Next Stage Scope

The next implementation stage should be limited to the settings sidebar/card
rewrite. Do not combine it with the post-processing scheduler rewrite.

Include:

- sidebar sections;
- card layout;
- tool status cards;
- select-folder actions for FFmpeg, Whisper, and VOT;
- existing FFmpeg install flow;
- language combo boxes;
- workflow disabled states and tooltips;
- Whisper model dialog shell if it ports cleanly.

Defer:

- real Whisper install progress wiring;
- real VOT install progress wiring;
- CUDA self-test/fallback;
- custom affected-file overwrite dialog;
- post-processing waiting queue.

## Additional Resolved Decisions

- Settings rewrite happens before installer wiring in the temporary UI.
- Install/select/model actions are not duplicated in workflow sections.
- Disabled FFmpeg/tool-gated controls are visible, non-clickable, and explain the
  missing dependency by tooltip.
- If settings are opened from a readiness dialog and the user installs or
  selects a tool, the original operation does not auto-start. The user clicks
  `Транскрибировать` or `Перевести` again.
- Affected-file confirmation happens before queueing, not when a queued
  operation later starts.
- Queued operations keep the settings snapshot captured at click time.
- If a queued operation loses its tool/model/media path before start, it fails
  gracefully and returns the task to completed state.
- Waiting post-processing operations are discarded on application close and are
  never written to `download_queue.json`.
