#pragma once

#include <cstdint>
#include <filesystem>

bool MoveFileReplacing(const std::filesystem::path& source, const std::filesystem::path& destination);

void CommitDownloadedFile(
    const std::filesystem::path& staged,
    const std::filesystem::path& target,
    std::uint64_t downloaded,
    std::uint64_t expectedSize
);
