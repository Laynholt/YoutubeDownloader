# YoutubeDownloader

YoutubeDownloader is a portable Win32 application for downloading YouTube videos, playlists, and audio through `yt-dlp`. It provides a native Windows UI, a download queue, title and thumbnail previews, FFmpeg-based media merging/processing, local transcription, and voice-over translation workflows.

## Screenshots

![Downloading video](screenshots/downloading.png)

![Downloads settings](screenshots/settings1.png)

![Transcription settings](screenshots/settings2.png)

![Translation settings](screenshots/settings3.png)

![Additional settings](screenshots/settings4.png)

![Tools settings](screenshots/settings5.png)

## Stack

- C++20
- CMake 3.20+
- Native Win32 UI: GDI+, DWM, Common Controls
- WinHTTP for HTTP requests and file downloads
- `yt-dlp` for metadata and media downloads
- FFmpeg/FFprobe for media merging and processing
- `nlohmann/json` single-header library in `third_party/`
- MSVC/Visual Studio toolchain for Windows

## Build

Requirements:

- Windows
- Visual Studio 2022 with the `Desktop development with C++` workload
- CMake 3.20 or newer

Configure and build a release version:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

With the Visual Studio generator, the executable is written to `build/bin/Release/`.

Run tests:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## Runtime

On startup, the application checks `yt-dlp` and can install or update it from official GitHub releases. FFmpeg can be selected manually, found through `PATH`, or installed into the application's local tools folder.

Optional tool integrations include:

- `whisper.cpp` for local transcription.
- `vot_exe` for subtitle retrieval and voice-over translation.

## Languages

Russian is built into the application. Extra interface translations are loaded from `stuff/languages/*.json`.

Translation file format, key rules, and fallback behavior are documented in [`stuff/languages/README.md`](stuff/languages/README.md). The bundled English translation is [`stuff/languages/en.json`](stuff/languages/en.json).

## License

This project is licensed under the MIT License. See [`LICENSE`](LICENSE).

Third-party libraries and external tools keep their own licenses. See [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
