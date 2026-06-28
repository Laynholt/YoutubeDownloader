#include "PostProcessingFileOps.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <chrono>
#include <system_error>

namespace {

bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec);
}

} // namespace

bool ReplaceOriginalMediaWithPostProcessedFile(
    const std::filesystem::path& replacement,
    const std::filesystem::path& original
) {
    if (!IsRegularFile(replacement)) {
        return false;
    }

    std::error_code ec;
    if (!IsRegularFile(original)) {
        std::filesystem::rename(replacement, original, ec);
        return !ec && IsRegularFile(original);
    }

    const BOOL replaced = ReplaceFileW(
        original.c_str(),
        replacement.c_str(),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (replaced) {
        return true;
    }

    const BOOL moved = MoveFileExW(
        replacement.c_str(),
        original.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    );
    return moved && IsRegularFile(original);
}

bool CommitPostProcessingSidecarFile(
    const std::filesystem::path& staged,
    const std::filesystem::path& destination
) {
    if (!IsRegularFile(staged)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        return false;
    }

    const bool destinationExists = IsRegularFile(destination);
    if (!destinationExists) {
        std::filesystem::rename(staged, destination, ec);
        if (!ec && IsRegularFile(destination)) {
            return true;
        }
        ec.clear();
        std::filesystem::copy_file(staged, destination, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec || !IsRegularFile(destination)) {
            std::filesystem::remove(destination, ec);
            return false;
        }
        ec.clear();
        std::filesystem::remove(staged, ec);
        return true;
    }

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path replacement = destination;
    replacement += L".post-processing.";
    replacement += std::to_wstring(nonce);
    replacement += L".tmp";

    std::filesystem::remove(replacement, ec);
    ec.clear();
    std::filesystem::copy_file(staged, replacement, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec || !IsRegularFile(replacement)) {
        std::filesystem::remove(replacement, ec);
        return false;
    }

    const BOOL replaced = ReplaceFileW(
        destination.c_str(),
        replacement.c_str(),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (!replaced) {
        const BOOL moved = MoveFileExW(
            replacement.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        );
        if (!moved) {
            std::filesystem::remove(replacement, ec);
            return false;
        }
    }

    std::filesystem::remove(staged, ec);
    return IsRegularFile(destination);
}
