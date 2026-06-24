#include "DownloadQueue.h"

#include "ProcessRunner.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <ranges>
#include <string_view>
#include <system_error>

namespace {

void AddUniquePath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    if (std::ranges::find(paths, path) == paths.end()) {
        paths.push_back(path);
    }
}

std::uint64_t FileSizeIfExists(const std::filesystem::path& path) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec)) {
        return 0;
    }
    return static_cast<std::uint64_t>(std::filesystem::file_size(path, ec));
}

std::uint64_t DiskBytesForPaths(const std::vector<std::filesystem::path>& paths) {
    std::uint64_t total = 0;
    for (const std::filesystem::path& path : paths) {
        total += FileSizeIfExists(path);
        total += FileSizeIfExists(std::filesystem::path(path.wstring() + L".part"));
    }
    return total;
}

std::wstring ExtractQueryValue(const std::wstring& url, std::wstring_view key) {
    const std::wstring needle = std::wstring(key) + L"=";
    size_t pos = url.find(needle);
    if (pos == std::wstring::npos) {
        return {};
    }
    pos += needle.size();
    const size_t end = url.find_first_of(L"&#?", pos);
    return url.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
}

std::wstring ExtractLikelyVideoId(const std::wstring& url) {
    std::wstring id = ExtractQueryValue(url, L"v");
    if (!id.empty()) {
        return id;
    }

    const std::array<std::wstring_view, 3> markers = {
        L"youtu.be/",
        L"/shorts/",
        L"/embed/"
    };
    for (std::wstring_view marker : markers) {
        const size_t markerPos = url.find(marker);
        if (markerPos == std::wstring::npos) {
            continue;
        }
        const size_t idStart = markerPos + marker.size();
        const size_t idEnd = url.find_first_of(L"/?&#", idStart);
        id = url.substr(idStart, idEnd == std::wstring::npos ? std::wstring::npos : idEnd - idStart);
        if (!id.empty()) {
            return id;
        }
    }
    return {};
}

void RemoveKnownPartialFilesFor(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(std::filesystem::path(path.wstring() + L".part"), ec);

    const std::filesystem::path parent = path.parent_path();
    if (parent.empty() || !std::filesystem::is_directory(parent, ec)) {
        return;
    }

    const std::wstring prefix = path.filename().wstring() + L".part";
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(parent, ec)) {
        if (ec) {
            break;
        }
        const std::wstring name = entry.path().filename().wstring();
        if (name.starts_with(prefix)) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

void RemovePartialFilesByVideoId(const std::filesystem::path& outputDirectory, const std::wstring& videoId) {
    if (outputDirectory.empty() || videoId.empty()) {
        return;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(outputDirectory, ec)) {
        return;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(outputDirectory, ec)) {
        if (ec) {
            break;
        }
        const std::filesystem::path path = entry.path();
        const std::wstring name = path.filename().wstring();
        if (name.find(videoId) == std::wstring::npos) {
            continue;
        }
        if (name.ends_with(L".part") || name.find(L".part-") != std::wstring::npos) {
            std::filesystem::remove(path, ec);
        }
    }
}

void RemoveInvalidTaskFiles(const DownloadTaskSnapshot& task) {
    for (const std::filesystem::path& path : task.outputFiles) {
        RemoveKnownPartialFilesFor(path);
    }
    RemovePartialFilesByVideoId(task.request.outputDirectory, ExtractLikelyVideoId(task.request.url));
}

bool IsPlaceholderTitle(const DownloadTaskSnapshot& task) {
    return task.title.empty() || task.title == task.request.url;
}

bool EnrichTaskMetadata(DownloadTaskSnapshot& task, std::wstring title, std::filesystem::path thumbnailPath) {
    bool changed = false;
    if (!title.empty() && title != task.request.url && IsPlaceholderTitle(task)) {
        task.title = std::move(title);
        changed = true;
    }
    if (!thumbnailPath.empty() && task.thumbnailPath.empty()) {
        task.thumbnailPath = std::move(thumbnailPath);
        changed = true;
    }
    return changed;
}

} // namespace

DownloadQueue::DownloadQueue(int maxParallelDownloads)
    : m_maxParallelDownloads(std::max(1, maxParallelDownloads)),
      m_executor([this](const DownloadTaskSnapshot& task, const DownloadTaskCallbacks& callbacks) {
          return DefaultExecutor(task, callbacks);
      }),
      m_scheduler(&DownloadQueue::SchedulerLoop, this) {
}

DownloadQueue::~DownloadQueue() {
    Shutdown();
}

void DownloadQueue::SetExecutor(DownloadTaskExecutor executor) {
    std::lock_guard lock(m_mutex);
    m_executor = std::move(executor);
}

int DownloadQueue::Enqueue(const YtDlpDownloadRequest& request, std::wstring title, std::filesystem::path thumbnailPath) {
    std::lock_guard lock(m_mutex);
    for (auto& [id, task] : m_tasks) {
        if (task.snapshot.request.url == request.url) {
            if (EnrichTaskMetadata(task.snapshot, std::move(title), std::move(thumbnailPath))) {
                ++m_revision;
            }
            return id;
        }
    }

    const int id = m_nextId++;
    TaskRecord record;
    record.snapshot.id = id;
    record.snapshot.request = request;
    record.snapshot.title = std::move(title);
    record.snapshot.thumbnailPath = std::move(thumbnailPath);
    record.snapshot.state = DownloadTaskState::Queued;
    record.snapshot.statusText = L"В очереди";
    m_tasks[id] = std::move(record);
    ++m_revision;
    m_cv.notify_all();
    return id;
}

bool DownloadQueue::EnrichMetadata(const std::wstring& url, std::wstring title, std::filesystem::path thumbnailPath) {
    std::lock_guard lock(m_mutex);
    for (auto& [id, task] : m_tasks) {
        UNREFERENCED_PARAMETER(id);
        if (task.snapshot.request.url == url &&
            EnrichTaskMetadata(task.snapshot, std::move(title), std::move(thumbnailPath))) {
            ++m_revision;
            return true;
        }
    }
    return false;
}

bool DownloadQueue::Cancel(int id) {
    std::lock_guard lock(m_mutex);
    auto it = m_tasks.find(id);
    if (it == m_tasks.end()) {
        return false;
    }
    TaskRecord& task = it->second;
    if (task.snapshot.state == DownloadTaskState::Queued) {
        task.snapshot.state = DownloadTaskState::Canceled;
        task.snapshot.statusText = L"Отменено";
        ++m_revision;
        m_cv.notify_all();
        return true;
    }
    if (task.active) {
        task.cancelRequested = true;
        task.snapshot.statusText = L"Отмена...";
        ++m_revision;
        return true;
    }
    return false;
}

bool DownloadQueue::Retry(int id) {
    std::lock_guard lock(m_mutex);
    auto it = m_tasks.find(id);
    if (it == m_tasks.end()) {
        return false;
    }
    TaskRecord& task = it->second;
    if (task.active ||
        (task.snapshot.state != DownloadTaskState::Failed && task.snapshot.state != DownloadTaskState::Canceled)) {
        return false;
    }
    task.cancelRequested = false;
    task.snapshot.state = DownloadTaskState::Queued;
    task.snapshot.percent = 0.0;
    task.snapshot.errorText.clear();
    task.snapshot.statusText = L"В очереди";
    task.snapshot.lastOutputLine.clear();
    task.snapshot.downloadedBytes = 0;
    task.snapshot.totalBytes = 0;
    task.snapshot.speedBytesPerSecond = 0;
    task.snapshot.etaSeconds = 0;
    task.snapshot.mediaKind.clear();
    task.snapshot.formatId.clear();
    task.snapshot.extension.clear();
    task.snapshot.resolution.clear();
    ++m_revision;
    m_cv.notify_all();
    return true;
}

bool DownloadQueue::DeleteFiles(int id) {
    std::lock_guard lock(m_mutex);
    auto it = m_tasks.find(id);
    if (it == m_tasks.end() || it->second.active) {
        return false;
    }

    const DownloadTaskState state = it->second.snapshot.state;
    if (state == DownloadTaskState::Canceled || state == DownloadTaskState::Failed) {
        RemoveInvalidTaskFiles(it->second.snapshot);
    }
    m_tasks.erase(it);
    ++m_revision;
    m_cv.notify_all();
    return true;
}

size_t DownloadQueue::ClearQueued() {
    std::lock_guard lock(m_mutex);
    size_t removed = 0;
    for (auto it = m_tasks.begin(); it != m_tasks.end();) {
        if (!it->second.active && it->second.snapshot.state == DownloadTaskState::Queued) {
            it = m_tasks.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed > 0) {
        ++m_revision;
        m_cv.notify_all();
    }
    return removed;
}

size_t DownloadQueue::ClearFinished() {
    std::lock_guard lock(m_mutex);
    size_t removed = 0;
    for (auto it = m_tasks.begin(); it != m_tasks.end();) {
        const DownloadTaskState state = it->second.snapshot.state;
        if (!it->second.active &&
            (state == DownloadTaskState::Completed || state == DownloadTaskState::Failed || state == DownloadTaskState::Canceled)) {
            if (state == DownloadTaskState::Failed || state == DownloadTaskState::Canceled) {
                RemoveInvalidTaskFiles(it->second.snapshot);
            }
            it = m_tasks.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed > 0) {
        ++m_revision;
        m_cv.notify_all();
    }
    return removed;
}

DownloadTaskSnapshot DownloadQueue::GetTask(int id) const {
    std::lock_guard lock(m_mutex);
    const auto it = m_tasks.find(id);
    if (it == m_tasks.end()) {
        return {};
    }
    return it->second.snapshot;
}

std::vector<DownloadTaskSnapshot> DownloadQueue::Snapshot() const {
    std::lock_guard lock(m_mutex);
    std::vector<DownloadTaskSnapshot> result;
    result.reserve(m_tasks.size());
    for (const auto& [id, task] : m_tasks) {
        UNREFERENCED_PARAMETER(id);
        result.push_back(task.snapshot);
    }
    return result;
}

std::uint64_t DownloadQueue::Revision() const {
    std::lock_guard lock(m_mutex);
    return m_revision;
}

void DownloadQueue::WaitForIdle() {
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [this]() {
        if (m_activeCount > 0) {
            return false;
        }
        return std::none_of(m_tasks.begin(), m_tasks.end(), [](const auto& item) {
            return item.second.snapshot.state == DownloadTaskState::Queued;
        });
    });
}

void DownloadQueue::Shutdown() {
    {
        std::lock_guard lock(m_mutex);
        if (m_shutdown) {
            return;
        }
        m_shutdown = true;
        for (auto& [id, task] : m_tasks) {
            UNREFERENCED_PARAMETER(id);
            task.cancelRequested = true;
        }
    }
    m_cv.notify_all();
    if (m_scheduler.joinable()) {
        m_scheduler.join();
    }
}

void DownloadQueue::SchedulerLoop() {
    while (true) {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this]() {
            if (m_shutdown) {
                return true;
            }
            if (m_activeCount >= m_maxParallelDownloads) {
                return false;
            }
            return std::any_of(m_tasks.begin(), m_tasks.end(), [](const auto& item) {
                return item.second.snapshot.state == DownloadTaskState::Queued;
            });
        });

        if (m_shutdown) {
            break;
        }

        while (m_activeCount < m_maxParallelDownloads) {
            auto it = std::find_if(m_tasks.begin(), m_tasks.end(), [](const auto& item) {
                return item.second.snapshot.state == DownloadTaskState::Queued;
            });
            if (it == m_tasks.end()) {
                break;
            }
            const int id = it->first;
            it->second.active = true;
            it->second.snapshot.state = DownloadTaskState::Preparing;
            it->second.snapshot.statusText = L"Подготовка";
            ++m_revision;
            ++m_activeCount;
            std::thread(&DownloadQueue::StartTask, this, id).detach();
        }
    }
}

void DownloadQueue::StartTask(int id) {
    DownloadTaskSnapshot task;
    DownloadTaskExecutor executor;
    {
        std::lock_guard lock(m_mutex);
        auto it = m_tasks.find(id);
        if (it == m_tasks.end()) {
            return;
        }
        task = it->second.snapshot;
        executor = m_executor;
    }

    DownloadTaskCallbacks callbacks;
    callbacks.onProgress = [this, id](double percent, const std::wstring& status) {
        std::lock_guard lock(m_mutex);
        auto it = m_tasks.find(id);
        if (it == m_tasks.end()) {
            return;
        }
        it->second.snapshot.state = DownloadTaskState::Downloading;
        it->second.snapshot.percent = std::max(it->second.snapshot.percent, std::clamp(percent, 0.0, 100.0));
        it->second.snapshot.statusText = status;
        ++m_revision;
    };
    callbacks.onProgressDetails = [this, id](const YtDlpProgress& progress) {
        std::lock_guard lock(m_mutex);
        auto it = m_tasks.find(id);
        if (it == m_tasks.end()) {
            return;
        }
        DownloadTaskSnapshot& snapshot = it->second.snapshot;
        if (snapshot.mediaKind == L"audio" && progress.mediaKind != L"audio") {
            return;
        }
        const bool switchedTrack =
            !snapshot.mediaKind.empty() &&
            !progress.mediaKind.empty() &&
            snapshot.mediaKind != progress.mediaKind;

        it->second.snapshot.state = DownloadTaskState::Downloading;
        it->second.snapshot.statusText = progress.stage;
        const std::uint64_t diskBytes = DiskBytesForPaths(snapshot.outputFiles);
        const std::uint64_t reportedBytes = diskBytes > 0 ? diskBytes : progress.downloadedBytes;
        if (switchedTrack) {
            snapshot.percent = std::clamp(progress.percent, 0.0, 100.0);
            snapshot.downloadedBytes = reportedBytes;
            snapshot.totalBytes = std::max(progress.totalBytes, reportedBytes);
        } else {
            snapshot.downloadedBytes = std::max(snapshot.downloadedBytes, reportedBytes);
            snapshot.totalBytes = std::max({snapshot.totalBytes, progress.totalBytes, snapshot.downloadedBytes});
            double normalizedPercent = std::clamp(progress.percent, 0.0, 100.0);
            if (snapshot.totalBytes > 0) {
                normalizedPercent = std::max(
                    normalizedPercent,
                    (static_cast<double>(snapshot.downloadedBytes) / static_cast<double>(snapshot.totalBytes)) * 100.0
                );
            }
            snapshot.percent = std::max(snapshot.percent, std::clamp(normalizedPercent, 0.0, 100.0));
        }
        snapshot.speedBytesPerSecond = progress.speedBytesPerSecond;
        snapshot.etaSeconds = progress.etaSeconds;
        if (!progress.mediaKind.empty()) {
            snapshot.mediaKind = progress.mediaKind;
        }
        snapshot.formatId = progress.formatId;
        snapshot.extension = progress.extension;
        snapshot.resolution = progress.resolution;
        ++m_revision;
    };
    callbacks.onPostProcessing = [this, id](double percent, const std::wstring& status) {
        std::lock_guard lock(m_mutex);
        auto it = m_tasks.find(id);
        if (it == m_tasks.end()) {
            return;
        }
        it->second.snapshot.state = DownloadTaskState::PostProcessing;
        it->second.snapshot.percent = std::clamp(percent, 0.0, 100.0);
        it->second.snapshot.statusText = status;
        it->second.snapshot.speedBytesPerSecond = 0;
        it->second.snapshot.etaSeconds = 0;
        ++m_revision;
    };
    callbacks.onOutputLine = [this, id](const std::wstring& line) {
        std::lock_guard lock(m_mutex);
        auto it = m_tasks.find(id);
        if (it != m_tasks.end()) {
            it->second.snapshot.lastOutputLine = line;
            AddUniquePath(it->second.snapshot.outputFiles, ExtractYtDlpOutputPath(line));
            ++m_revision;
        }
    };
    callbacks.isCanceled = [this, id]() {
        std::lock_guard lock(m_mutex);
        auto it = m_tasks.find(id);
        return it == m_tasks.end() || it->second.cancelRequested || m_shutdown;
    };

    DownloadTaskResult result;
    try {
        result = executor(task, callbacks);
    } catch (const std::exception& ex) {
        result.success = false;
        result.errorText.assign(ex.what(), ex.what() + std::strlen(ex.what()));
    } catch (...) {
        result.success = false;
        result.errorText = L"Неизвестная ошибка";
    }

    {
        std::lock_guard lock(m_mutex);
        auto it = m_tasks.find(id);
        if (it != m_tasks.end()) {
            it->second.active = false;
            --m_activeCount;
            if (it->second.cancelRequested) {
                it->second.snapshot.state = DownloadTaskState::Canceled;
                it->second.snapshot.statusText = L"Отменено";
                ++m_revision;
            } else if (result.success) {
                it->second.snapshot.state = DownloadTaskState::Completed;
                it->second.snapshot.percent = 100.0;
                it->second.snapshot.statusText = result.statusText.empty() ? L"Готово" : result.statusText;
                for (const std::filesystem::path& path : result.outputFiles) {
                    AddUniquePath(it->second.snapshot.outputFiles, path);
                }
                ++m_revision;
            } else {
                it->second.snapshot.state = DownloadTaskState::Failed;
                it->second.snapshot.errorText = result.errorText;
                it->second.snapshot.statusText = L"Ошибка";
                ++m_revision;
            }
        }
    }
    m_cv.notify_all();
}

DownloadTaskResult DownloadQueue::DefaultExecutor(
    const DownloadTaskSnapshot& task,
    const DownloadTaskCallbacks& callbacks
) {
    ProcessRunOptions options;
    options.executable = task.request.ytDlpExePath.empty() ? std::filesystem::path(L"yt-dlp.exe") : task.request.ytDlpExePath;
    options.arguments = BuildDownloadArguments(task.request);
    options.timeoutMs = INFINITE;
    auto handleLine = [callbacks](const std::wstring& line) {
        if (callbacks.onOutputLine) {
            callbacks.onOutputLine(line);
        }
        const YtDlpProcessLine parsed = ParseYtDlpProcessLine(line);
        if (parsed.progress.recognized && callbacks.onProgressDetails) {
            callbacks.onProgressDetails(parsed.progress);
        }
    };
    options.onStdoutLine = handleLine;
    options.onStderrLine = handleLine;

    const ProcessRunResult result = ProcessRunner::Run(options);
    if (callbacks.isCanceled && callbacks.isCanceled()) {
        return {false, L"Отменено", {}};
    }
    if (result.exitCode != 0) {
        return {false, result.stderrText.empty() ? result.stdoutText : result.stderrText, {}};
    }
    return {true, L"", {}};
}
