#pragma once

#include <filesystem>
#include <utility>

class AppPaths {
public:
    explicit AppPaths(std::filesystem::path root)
        : m_root(std::move(root)) {
    }

    const std::filesystem::path& root() const { return m_root; }
    std::filesystem::path stuffDir() const { return m_root / L"stuff"; }
    std::filesystem::path configPath() const { return stuffDir() / L"config.ini"; }
    std::filesystem::path logPath() const { return stuffDir() / L"ytdl.log"; }
    std::filesystem::path downloadQueuePath() const { return stuffDir() / L"download_queue.json"; }
    std::filesystem::path thumbCacheDir() const { return stuffDir() / L"thumb_cache"; }
    std::filesystem::path languagesDir() const { return stuffDir() / L"languages"; }
    std::filesystem::path toolsDir() const { return m_root / L"tools"; }
    std::filesystem::path ytDlpDir() const { return toolsDir() / L"yt-dlp"; }
    std::filesystem::path ytDlpExePath() const { return ytDlpDir() / L"yt-dlp.exe"; }
    std::filesystem::path ytDlpVersionPath() const { return ytDlpDir() / L"version.txt"; }
    std::filesystem::path localFfmpegBinDir() const { return toolsDir() / L"ffmpeg" / L"bin"; }
    std::filesystem::path localFfmpegExePath() const { return localFfmpegBinDir() / L"ffmpeg.exe"; }
    std::filesystem::path localFfprobeExePath() const { return localFfmpegBinDir() / L"ffprobe.exe"; }
    std::filesystem::path localFfplayExePath() const { return localFfmpegBinDir() / L"ffplay.exe"; }
    std::filesystem::path localFfmpegVersionPath() const { return localFfmpegBinDir().parent_path() / L"version.txt"; }
    std::filesystem::path localWhisperDir() const { return toolsDir() / L"whisper"; }
    std::filesystem::path localWhisperCpuDir() const { return localWhisperDir() / L"cpu"; }
    std::filesystem::path localWhisperCudaDir() const { return localWhisperDir() / L"cuda"; }
    std::filesystem::path localWhisperCpuExePath() const { return localWhisperCpuDir() / L"whisper-cli.exe"; }
    std::filesystem::path localWhisperCpuVersionPath() const { return localWhisperCpuDir() / L"version.txt"; }
    std::filesystem::path localWhisperCudaExePath() const { return localWhisperCudaDir() / L"whisper-cli.exe"; }
    std::filesystem::path localWhisperCudaVersionPath() const { return localWhisperCudaDir() / L"version.txt"; }
    std::filesystem::path localWhisperModelsDir() const { return localWhisperDir() / L"models"; }
    std::filesystem::path localWhisperModelPath() const { return localWhisperModelsDir() / L"ggml-large-v3-turbo.bin"; }
    std::filesystem::path localVotDir() const { return toolsDir() / L"vot"; }
    std::filesystem::path localVotExePath() const { return localVotDir() / L"vot-helper.exe"; }
    std::filesystem::path localVotVersionPath() const { return localVotDir() / L"version.txt"; }
    std::filesystem::path transcriptionTempDir() const { return stuffDir() / L"transcription_tmp"; }
    std::filesystem::path voiceOverTempDir() const { return stuffDir() / L"voiceover_tmp"; }

private:
    std::filesystem::path m_root;
};
