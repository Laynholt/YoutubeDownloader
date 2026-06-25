# YoutubeDownloader UX Refresh Design

Date: 2026-06-25

## Goal

Improve the overloaded UX while keeping the application a focused native Windows desktop tool. The target feel is inspired by the supplied first settings screenshot: dark surface, red accent, left navigation, calm cards, segmented controls, and clearer hierarchy. The design should not copy the reference 1:1 or introduce heavy visual effects that make the Win32 implementation fragile.

## Scope

The refresh targets the noisiest surfaces:

- Settings: structural redesign into a resizable shell with left navigation and card-based content.
- Tools: cleaner FFmpeg, Whisper.cpp, and vot-cli status cards with technical paths hidden by default.
- Install/progress dialogs: keep separate modal windows, but align visual treatment with the new system.
- Main window: light hierarchy and cosmetic cleanup only, without large workflow changes.

Out of scope:

- Replacing the native Win32 UI stack.
- Redesigning download queue behavior or task lifecycle.
- Adding a full onboarding wizard.
- Keeping Kazakh voice-over as an option.

## Navigation

Settings use a left navigation rail with four real sections:

- Downloads
- Voice-over
- Tools
- About

The rail has two modes:

- Expanded by default: icon plus label.
- Collapsed: icons only, active indicator, hover tooltip, and a clear expand control.

The settings window is resizable with a minimum size. The rail can be collapsed manually, and also collapses automatically below a narrow width. Manual preference is persisted, but automatic collapse takes precedence when needed to preserve layout.

## Settings Layout

The dialog keeps the existing `Cancel` / `Save` model. Internally, settings continue to edit a working config until saved. The action buttons are fixed at the bottom of the dialog.

Each section has:

- A header with the section title and short description.
- Medium-density cards with a title, optional muted helper text, and the controls for that setting.
- Stable spacing and fixed control heights so the dialog does not shift while interacting.

## Downloads Section

Cards:

- Quality: segmented control with Audio, 360p, 480p, 720p, 1080p, Max.
- Format: segmented control with Auto, MP4, MKV, WEBM.
- Behavior: auto-check app/tools, transcribe after download, and max parallel downloads stepper.

Parallel downloads keep the existing clamp range.

## Voice-over Section

Cards:

- Mode: Separate track or lower original volume.
- Result language: Russian and English only.

Kazakh is removed entirely from the visible product surface. During implementation:

- Remove the `KK` button and Kazakh tooltip.
- Remove Kazakh from the settings handler.
- Treat old `voice_over_language` values of `kk` or `kaz` as `ru` when loading config.
- Do not expose Kazakh in the voice-over language mapping used for new requests.

Existing files already generated with Kazakh naming are not deleted or renamed.

## Tools Section

Cards:

- FFmpeg
- Whisper.cpp
- vot-cli

Each card shows a short status by default, such as installed/missing, active backend, selected model, or Node availability. Long executable/model paths are hidden by default.

Each tool card has an inline details disclosure. Opening details expands the same card and shows diagnostic rows such as executable path, model path, Node path, backend, and copy actions. Install, choose, configure, and model actions remain in the card.

Install and download progress stay in separate compact modal dialogs. The main Tools section should not become an installer wizard.

## About Section

The About section is integrated into the settings shell:

- Application name and version.
- Update check action.
- Short app description.

The existing separate About dialog can be removed from settings navigation if this section covers the same content. The main window may still route "About" to the settings About section or a compatible modal, depending on implementation cost.

## Main Window Cleanup

The main window receives limited visual hierarchy cleanup:

- Make the URL input the primary focus.
- Keep the preview area but reduce visual weight.
- Group the download action and tool readiness status more clearly.
- Make Settings, Logs, and clear-completed actions secondary.
- Keep the queue as the large working area.

No major main-window workflow redesign is part of this spec.

## Visual System

Use the existing dark theme and red accent. Add or refine reusable drawing helpers where useful:

- Panels/cards with consistent border radius and border color.
- Segmented controls.
- Switch-like toggles or clearly selected state buttons.
- Inline disclosure/details rows.
- Sidebar item rendering for expanded and collapsed modes.

Avoid heavy glass blur or complex effects that are expensive or brittle in GDI+/Win32.

## Error Handling

Tool cards should clearly show missing or invalid dependencies without dumping paths as primary content. Details should expose enough diagnostic information for support:

- Full path rows use ellipsis in normal layout.
- Copy actions copy exact full paths where available.
- Missing tools show the primary next action, such as Install or Choose.

If an old config contains unsupported voice-over language `kk` or `kaz`, loading normalizes it to `ru`.

## Testing

Required verification:

- Existing core tests still pass.
- Config round-trip covers supported voice-over languages `ru` and `en`.
- Config loading normalizes unsupported old `kk`/`kaz` values to `ru`.
- Settings dialog interactions update working config and only persist on Save.
- Tools details can expand/collapse without losing status/action layout.
- Narrow settings width collapses the sidebar and preserves readable content.
- Main window smoke check verifies controls still lay out without overlap.

Manual visual QA:

- Settings expanded and collapsed sidebar.
- Downloads, Voice-over, Tools, About sections.
- Tools details open and closed.
- Install/progress modal after visual refresh.
- Main window at normal and smaller widths.
