# SponsorBlock Integration Design

## Goal

Add one download setting that lets the user keep all content, remove paid sponsor integrations, or remove both paid sponsor integrations and creator self-promotion.

## User interface

The Downloads settings page gains a SponsorBlock card between Container and Parallelism.

The card contains one existing custom-drawn combo button with three mutually exclusive values:

- `off`: do not remove segments;
- `sponsor`: remove paid sponsor integrations;
- `sponsor_selfpromo`: remove paid sponsor integrations and creator self-promotion.

The default is `off`. The button and menu reuse the existing settings combo rendering and interaction; no new control class is introduced. All visible labels and descriptions use the existing localization system.

## Persistence and queue behavior

`AppConfig` stores the selected mode as a validated string. Unknown persisted values fall back to `off`.

Each `YtDlpDownloadRequest` receives a copy of the selected mode when the task is enqueued. `DownloadQueueStore` persists it with the task so restored and retried downloads retain the mode that was selected when they were added, even if the global setting changes later.

## Download flow

`BuildDownloadArguments` maps the mode directly to yt-dlp's built-in SponsorBlock support:

| Mode | yt-dlp arguments |
| --- | --- |
| `off` | no SponsorBlock argument |
| `sponsor` | `--sponsorblock-remove sponsor` |
| `sponsor_selfpromo` | `--sponsorblock-remove sponsor,selfpromo` |

yt-dlp fetches the SponsorBlock segments and invokes FFmpeg as part of its normal post-processing. The application does not implement a second HTTP client, SponsorBlock parser, interval merger, or FFmpeg cutting pipeline.

If SponsorBlock has no matching segments, yt-dlp leaves the downloaded media unchanged. API and FFmpeg diagnostics continue through the existing yt-dlp output and task error flow. Cancellation continues to use the existing download cancellation path.

## Validation

Automated checks cover:

- the default mode and config save/load round trip;
- fallback from an unknown config value to `off`;
- exact yt-dlp arguments for all three modes;
- download queue save/load round trip for the selected mode;
- localization completeness for the new UI text.

The existing core test executable and application build are the completion checks.

## References

- [yt-dlp SponsorBlock options](https://github.com/yt-dlp/yt-dlp/blob/master/README.md#sponsorblock-options)
- [SponsorBlock API documentation](https://wiki.sponsor.ajay.app/w/API_Docs)
