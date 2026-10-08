#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);
std::string PathToUtf8(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(const std::string& value);
std::wstring FormatBytes(std::uint64_t bytes);
std::wstring FormatProgressBytes(std::uint64_t downloaded, std::uint64_t total);
std::wstring FormatDuration(std::uint64_t seconds);
int CalculateProgressPercent(std::uint64_t downloaded, std::uint64_t total);
std::wstring ErrorSummary(std::wstring_view diagnostic);
std::wstring ErrorDetails(std::wstring_view diagnostic);
