cmake_minimum_required(VERSION 3.20)

file(STRINGS src/Core/AppVersion.h YTD_APP_VERSION_MAJOR_LINE REGEX "^#define YTD_APP_VERSION_MAJOR ")
file(STRINGS src/Core/AppVersion.h YTD_APP_VERSION_MINOR_LINE REGEX "^#define YTD_APP_VERSION_MINOR ")
file(STRINGS src/Core/AppVersion.h YTD_APP_VERSION_PATCH_LINE REGEX "^#define YTD_APP_VERSION_PATCH ")
string(REGEX REPLACE "^#define YTD_APP_VERSION_MAJOR ([0-9]+)$" "\\1" YTD_APP_VERSION_MAJOR "${YTD_APP_VERSION_MAJOR_LINE}")
string(REGEX REPLACE "^#define YTD_APP_VERSION_MINOR ([0-9]+)$" "\\1" YTD_APP_VERSION_MINOR "${YTD_APP_VERSION_MINOR_LINE}")
string(REGEX REPLACE "^#define YTD_APP_VERSION_PATCH ([0-9]+)$" "\\1" YTD_APP_VERSION_PATCH "${YTD_APP_VERSION_PATCH_LINE}")
set(YTD_APP_VERSION "${YTD_APP_VERSION_MAJOR}.${YTD_APP_VERSION_MINOR}.${YTD_APP_VERSION_PATCH}")

project(YoutubeDownloader
    VERSION ${YTD_APP_VERSION}
    DESCRIPTION "Portable Win32 YouTube downloader"
    LANGUAGES CXX RC
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX)
endif()

set(CORE_SOURCES
    src/Core/AsyncWait.cpp
    src/Core/BackendText.cpp
    src/Core/Config.cpp
    src/Core/DownloadQueue.cpp
    src/Core/DownloadQueueStore.cpp
    src/Core/FileOperations.cpp
    src/Core/Logger.cpp
    src/Core/Localization.cpp
    src/Core/PostProcessingFileOps.cpp
    src/Core/ToolManagers.cpp
    src/Core/TranscriptionClient.cpp
    src/Core/Version.cpp
    src/Core/VoiceOverTranslationClient.cpp
    src/Core/YtDlpClient.cpp
    src/Platform/ProcessRunner.cpp
    src/Platform/WinHttpClient.cpp
)

add_library(YoutubeDownloaderCore STATIC
    ${CORE_SOURCES}
)

target_include_directories(YoutubeDownloaderCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Platform
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if(MSVC)
    target_compile_options(YoutubeDownloaderCore PRIVATE /W4 /permissive- /utf-8)
endif()

target_link_libraries(YoutubeDownloaderCore PUBLIC
    winhttp
)

add_executable(YoutubeDownloader WIN32
    src/App/main.cpp
    src/UI/Application.cpp
    src/UI/DialogWindows.cpp
    src/UI/resource.rc
    src/UI/UiActions.cpp
    src/UI/UiRenderer.cpp
)

target_include_directories(YoutubeDownloader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI
)

target_link_libraries(YoutubeDownloader PRIVATE
    YoutubeDownloaderCore
    comctl32
    dwmapi
    gdiplus
    gdi32
    user32
    shell32
)

if(MSVC)
    target_compile_options(YoutubeDownloader PRIVATE /W4 /permissive- /utf-8)
endif()

set_target_properties(YoutubeDownloader PROPERTIES
    OUTPUT_NAME "YoutubeDownloader"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_custom_command(TARGET YoutubeDownloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/stuff/languages
        $<TARGET_FILE_DIR:YoutubeDownloader>/stuff/languages
)

add_executable(YoutubeDownloaderTests
    src/UI/UiActions.cpp
    tests/test_core.cpp
)

target_include_directories(YoutubeDownloaderTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI
)

target_link_libraries(YoutubeDownloaderTests PRIVATE
    YoutubeDownloaderCore
    user32
)

if(MSVC)
    target_compile_options(YoutubeDownloaderTests PRIVATE /W4 /permissive- /utf-8)
endif()

enable_testing()
add_test(NAME YoutubeDownloaderCoreTests COMMAND YoutubeDownloaderTests)
