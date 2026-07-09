# Third-party notices

This project source is licensed under the MIT License. Third-party libraries and external tools keep their own licenses.

## Bundled source dependency

- `nlohmann/json` (`third_party/nlohmann/json.hpp`): https://github.com/nlohmann/json, MIT License. The bundled header includes its SPDX notice.

## External tools used at runtime

The application can download or use these tools as separate executables. They are not relicensed by this project.

- `yt-dlp`: https://github.com/yt-dlp/yt-dlp, The Unlicense.
- FFmpeg: https://ffmpeg.org/legal.html, LGPLv2.1-or-later by default; GPLv2-or-later applies to FFmpeg builds that include GPL components. The application's automatic FFmpeg installer currently uses BtbN's `win64-gpl` build URL, so treat that downloaded FFmpeg package as GPL-covered.
- BtbN/FFmpeg-Builds scripts: https://github.com/BtbN/FFmpeg-Builds, MIT License. The produced FFmpeg binaries are governed by FFmpeg and included codec/library licenses.
- `whisper.cpp`: https://github.com/ggml-org/whisper.cpp, MIT License.
- `vot_exe`: https://github.com/Laynholt/vot_exe, MIT License.

If release packages ever bundle downloaded tool binaries, include the corresponding upstream license files and source/build information required by those tools.
