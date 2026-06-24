# Reliability and Code Cleanup Design

## Goal

Remove the identified P0, P1, and P2 defects, make background work lifetime-safe, enable useful per-run file logging, and delete dead or duplicated code without changing the application's visible workflow.

## Background task ownership

Long-lived application operations will use owned `std::jthread` instances rather than detached threads. `DownloadQueue` will own its scheduler and active workers and will not finish shutdown until every worker has stopped. Preview loading and background tool/update checks will also be owned by `Application`, canceled during replacement or shutdown, and joined before dependent state is destroyed.

Cancellation will use `std::stop_token` at the owning layer. Where `ProcessRunner` or WinHTTP requires a Win32 event, a small bridge will set that event when stop is requested. Reader threads for process stdout and stderr remain ordinary joinable `std::thread` objects because their lifetime is strictly contained inside a single `ProcessRunner::Run` call.

## Process output handling

`ProcessRunner` will preserve one persistent decoded-output offset per stream. Complete lines are emitted exactly once as chunks arrive, and the final unterminated line is emitted once at EOF. The complete stdout and stderr text remains available in `ProcessRunResult`.

Cancellation will terminate the launched process and wait for its contained reader threads. The queue will use one executor implementation rather than replacing a default executor with a duplicate application-specific executor.

## Preview behavior

URL edits will schedule a debounced preview request. A new edit cancels and joins the previous pending or active preview operation before it can publish a result. Only the most recent request may update UI state. Application shutdown stops the active request before destroying the main window or related state.

## HTTP and file replacement

Downloads will always write to a temporary file in the target directory. The implementation will check directory creation, every file write, final flush/close state, and downloaded byte count when `Content-Length` is known. Cancellation, read failure, write failure, or length mismatch removes the temporary file.

After successful validation, the temporary file replaces the target without first discarding a working target. yt-dlp updates will keep the existing executable until the replacement is ready and will restore or preserve it if replacement fails. Thumbnail caching will never accept a leftover partial file as a valid cached image.

## Runtime configuration

`DownloadQueue` will expose a thread-safe method to update its maximum parallel download count. Saving settings applies the new value immediately and wakes the scheduler. Existing active downloads continue; the scheduler starts no additional work until the active count is below the new limit.

## Logging

`stuff/ytdl.log`, beside `config.ini`, is truncated once when `Logger` is constructed during application startup. Subsequent writes are timestamped UTF-8 records protected by the logger mutex.

The application logs:

- startup and orderly shutdown;
- tool and application-update checks and their failures;
- preview start, cancellation, completion, and errors;
- queue insertion, retry, cancellation, completion, and failure;
- settings changes relevant to runtime behavior.

Raw continuous yt-dlp stdout/stderr is not logged. A bounded final diagnostic message is logged for failures.

## Dead and duplicated code removal

Remove members and paths with no production consumer:

- `DownloadTaskState::PostProcessing`;
- `DownloadTaskSnapshot::lastOutputLine`;
- `FfmpegStatus::ffprobeExe` and `FfmpegStatus::binDir`;
- `ReleaseAssetInfo::pageUrl`;
- `AppPaths::assetsDir()`;
- the unused default/duplicate download executor and obsolete callback surface that exists only for it;
- unused dialog overloads that have no application caller;
- unused intermediate result fields and assignments.

Keep `Logger` and make it operational as described above. Keep FFprobe and FFplay installation paths where they are used to install the bundled FFmpeg toolset.

## Error handling

Background results will use owned storage whose lifetime is tied to the receiving application rather than unchecked raw heap pointers passed through `PostMessageW`. Failed message delivery cannot leak result objects. Application shutdown is idempotent, checks COM initialization success, and balances `CoUninitialize` only after successful initialization.

User-facing behavior remains unchanged except that cancellation and shutdown become reliable, previews no longer spawn an operation for every keystroke, parallelism changes apply immediately, and operational failures are recorded in the log.

## Testing

Each behavior change follows red-green-refactor. Regression coverage will include:

- destroying a queue with an active executor and proving shutdown waits;
- exact-once stdout/stderr line callbacks across multiple chunks and EOF;
- immediate increase and decrease of queue parallelism;
- logger truncation at startup and append behavior within a run;
- failed or short HTTP writes leaving neither a valid target nor a reusable partial cache entry through testable file-finalization helpers;
- safe executable replacement preserving the old file on failure through a testable replacement helper;
- preview debounce/cancellation logic through a UI-independent coordinator or decision helper;
- removal of obsolete state and fields through compilation and the existing functional suite.

The final verification gate is a clean Release build, the complete CTest suite, MSVC `/W4`, and MSVC Code Analysis with no new project warnings.
