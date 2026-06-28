#pragma once

#include <filesystem>

bool ReplaceOriginalMediaWithPostProcessedFile(
    const std::filesystem::path& replacement,
    const std::filesystem::path& original
);

bool CommitPostProcessingSidecarFile(
    const std::filesystem::path& staged,
    const std::filesystem::path& destination
);
