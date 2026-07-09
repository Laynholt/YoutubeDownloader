#include "Localization.h"

#include "BackendText.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <set>
#include <vector>

namespace {

std::wstring NormalizeLanguageId(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch >= L'A' && ch <= L'Z') {
            out.push_back(static_cast<wchar_t>(ch - L'A' + L'a'));
        } else if ((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'_' || ch == L'-') {
            out.push_back(ch);
        } else {
            return {};
        }
    }
    return out.size() <= 32 ? out : std::wstring{};
}

std::unordered_map<std::wstring, std::wstring> RussianStrings() {
    return {
        {L"app.queued", L"В очереди"},
        {L"app.preparing", L"Подготовка"},
        {L"app.downloading", L"Скачивание"},
        {L"app.done", L"Готово"},
        {L"app.error", L"Ошибка"},
        {L"app.canceled", L"Отменено"},
        {L"app.failed_to_find_the_downloaded_media_file_for_this_task", L"Не удалось найти скачанный медиафайл для этой задачи.\n\n"},
        {L"app.task", L"Задача: "},
        {L"app.url", L"Ссылка: "},
        {L"app.the_queue_entry_has_no_output_file_path_yt_dlp_may_have", L"\nВ записи очереди нет пути к итоговому файлу. Возможно, yt-dlp завершил загрузку без передачи имени файла приложению."},
        {L"app.checked_paths", L"\nПроверенные пути:\n"},
        {L"app.check_that_the_file_was_not_deleted_moved_or_renamed_aft", L"\nПроверьте, что файл не был удалён, перемещён или переименован после загрузки."},
        {L"app.audio", L"Аудио"},
        {L"app.video", L"Видео"},
        {L"app.youtube_dlp_ready", L"youtube-dlp готов"},
        {L"app.youtube_dlp_not_found", L"youtube-dlp не найден"},
        {L"app.ffmpeg_found", L" · ffmpeg найден"},
        {L"app.copy_error", L"Скопировать ошибку"},
        {L"app.failed_to_initialize_gdi", L"Не удалось инициализировать GDI+."},
        {L"app.failed_to_register_the_button_class", L"Не удалось зарегистрировать класс кнопок."},
        {L"app.failed_to_register_the_window_class", L"Не удалось зарегистрировать класс окна."},
        {L"app.failed_to_create_the_main_window", L"Не удалось создать главное окно."},
        {L"app.queue_cleared_removed", L"Очередь очищена: удалено "},
        {L"app.tasks", L" задач"},
        {L"app.processing_is_still_running", L"Обработка ещё выполняется"},
        {L"app.wait_for_transcription_translation_to_finish_or_cancel_i", L"Дождитесь завершения или отмените транскрибацию/перевод перед очисткой завершённых задач."},
        {L"app.completed_tasks_cleared_removed", L"Завершённые задачи очищены: удалено "},
        {L"app.no_folder_selected", L"Папка не выбрана"},
        {L"app.specify_the_downloads_folder", L"Укажите папку загрузок."},
        {L"app.folder_unavailable", L"Папка недоступна"},
        {L"app.failed_to_open_or_create_the_downloads_folder", L"Не удалось открыть или создать папку загрузок."},
        {L"app.operation_failed", L"Операция не выполнена"},
        {L"app.unknown_error", L"Неизвестная ошибка"},
        {L"app.video_found", L"Видео найдено"},
        {L"app.playlist", L" — плейлист: "},
        {L"app.videos", L" видео"},
        {L"app.failed_to_fetch_preview", L"Не удалось получить preview"},
        {L"app.video_or_playlist_url", L"Ссылка на видео или плейлист"},
        {L"app.paste", L"Вставить"},
        {L"app.paste_a_video_or_playlist_link_above", L"Вставьте ссылку на видео или плейлист выше"},
        {L"app.choose", L"Выбрать..."},
        {L"app.download", L"Скачать"},
        {L"app.clear_queue", L"Очистить очередь"},
        {L"app.logs", L"Логи"},
        {L"app.settings", L"Настройки"},
        {L"app.preparing_interface", L"Подготовка интерфейса"},
        {L"app.download_queue", L"Очередь загрузок"},
        {L"app.no_tasks_yet", L"Задач пока нет"},
        {L"app.paste_a_video_playlist_or_other_supported_source_url", L"Вставьте ссылку на видео, плейлист или другой поддерживаемый источник."},
        {L"app.pastes_a_link_from_the_clipboard", L"Вставляет ссылку из буфера обмена."},
        {L"app.folder_where_downloaded_files_will_be_saved", L"Папка, куда будут сохраняться скачанные файлы."},
        {L"app.choose_the_folder_for_saved_downloads", L"Выберите папку для сохранения загрузок."},
        {L"app.adds_the_link_to_the_download_queue", L"Добавляет ссылку в очередь загрузок."},
        {L"app.removes_tasks_that_have_not_started_downloading_yet_acti", L"Удаляет из очереди задачи, которые ещё не начали загружаться. Активные загрузки не останавливаются."},
        {L"app.opens_the_current_downloads_folder", L"Открывает текущую папку загрузок."},
        {L"app.opens_the_current_log_file", L"Открывает текущий файл логов."},
        {L"app.clears_completed_and_failed_tasks_from_the_list_files_on", L"Очищает из списка завершённые и ошибочные задачи. Файлы на диске не удаляются."},
        {L"app.opens_quality_container_ffmpeg_and_application_behavior", L"Открывает настройки качества, контейнера, FFmpeg и поведения приложения."},
        {L"app.cancel", L"Отменить"},
        {L"app.resume", L"Возобновить"},
        {L"app.close", L"Закрыть"},
        {L"app.delete", L"Удалить"},
        {L"app.running", L"Выполнение..."},
        {L"app.remaining_in_queue", L"В очереди осталось "},
        {L"app.checking_yt_dlp", L"Проверка yt-dlp..."},
        {L"app.reading_information_please_wait", L"Идёт считывание информации, подождите"},
        {L"app.yt_dlp_ready", L"yt-dlp готов"},
        {L"app.yt_dlp_not_found", L"yt-dlp не найден"},
        {L"app.yt_dlp_preparation_error", L"Ошибка подготовки yt-dlp: "},
        {L"app.yt_dlp_preparation_error_2", L"Ошибка подготовки yt-dlp"},
        {L"app.unknown_update_check_error", L"Неизвестная ошибка проверки обновлений"},
        {L"app.paste_a_link_to_fetch_the_title_and_preview", L"Вставьте ссылку, чтобы получить название и превью"},
        {L"app.preview_unavailable", L"Preview недоступен: "},
        {L"app.preview_unavailable_2", L"Preview недоступен"},
        {L"app.yt_dlp_is_not_ready_yet", L"yt-dlp ещё не готов"},
        {L"app.wait_for_yt_dlp_check_installation_or_update_to_finish", L"Дождитесь завершения проверки, установки или обновления yt-dlp."},
        {L"app.information_is_still_loading", L"Информация ещё загружается"},
        {L"app.wait_until_the_application_fetches_the_title_playlist_it", L"Дождитесь, пока приложение получит название, состав плейлиста и превью."},
        {L"app.no_link", L"Нет ссылки"},
        {L"app.paste_a_video_or_playlist_url", L"Вставьте ссылку на видео или плейлист."},
        {L"app.adding_playlist_items_remaining", L"Добавление элементов плейлиста: осталось "},
        {L"app.settings_saved", L"Настройки сохранены"},
        {L"app.operation_already_added", L"Операция уже добавлена"},
        {L"app.transcription_or_translation_is_already_running_or_queue", L"Для этой задачи уже выполняется или ожидает транскрибация/перевод."},
        {L"app.file_not_found", L"Файл не найден"},
        {L"app.the_source_url_is_unavailable_for_this_task", L"Для этой задачи недоступна исходная ссылка."},
        {L"app.audio_file_subtitles_will_be_saved_as_separate_txt_srt_f", L"Аудиофайл: субтитры будут сохранены отдельными TXT/SRT"},
        {L"app.cuda_whisper_unavailable_switched_to_cpu", L"CUDA Whisper недоступен: переключено на CPU"},
        {L"app.ffmpeg_not_found_subtitles_will_be_saved_as_separate_fil", L"FFmpeg не найден: субтитры будут сохранены отдельными файлами"},
        {L"app.overwrite_transcription", L"Перезапись транскрибации"},
        {L"app.video_is_too_long", L"Видео слишком длинное"},
        {L"app.voice_over_translation_does_not_translate_videos_longer", L"Voice Over Translation не переводит видео длиннее 4 часов."},
        {L"app.audio_file_translation_will_be_saved_as_a_separate_mp3", L"Аудиофайл: перевод будет сохранён отдельным MP3"},
        {L"app.ffmpeg_not_found_translation_will_be_saved_as_a_separate", L"FFmpeg не найден: перевод будет сохранён отдельным MP3"},
        {L"app.overwrite_translation", L"Перезапись перевода"},
        {L"app.operation_added_to_the_processing_queue", L"Операция добавлена в очередь обработки"},
        {L"app.transcribing", L"Транскрибация..."},
        {L"app.translating", L"Перевод..."},
        {L"app.cuda_whisper_unavailable_retrying_on_cpu", L"CUDA Whisper недоступен: повтор на CPU"},
        {L"app.transcription_completed", L"Транскрибация завершена"},
        {L"app.transcription_failed", L"Транскрибация не выполнена"},
        {L"app.translation_completed", L"Перевод завершён"},
        {L"app.translation_failed", L"Перевод не выполнен"},
        {L"app.operation_removed_from_the_processing_queue", L"Операция удалена из очереди обработки"},
        {L"app.operation_canceled", L"Операция отменена"},
        {L"dialog.choose_the_ffmpeg_folder_or_the_bin_folder", L"Выберите папку FFmpeg или папку bin"},
        {L"dialog.russian", L"Русский  ▾"},
        {L"dialog.ffmpeg_not_found", L"FFmpeg не найден"},
        {L"dialog.whisper_not_found", L"Whisper не найден"},
        {L"dialog.whisper_cli_exe_was_not_found_in_the_selected_folder", L"В выбранной папке не найден whisper-cli.exe."},
        {L"dialog.whisper_does_not_start", L"Whisper не запускается"},
        {L"dialog.the_selected_whisper_cli_exe_was_found_but_failed_the_la", L"Выбранный whisper-cli.exe найден, но не прошёл проверку запуска. Выберите другую папку или установите Whisper.cpp заново."},
        {L"dialog.choose_vot_helper", L"Выберите VOT helper"},
        {L"dialog.vot_helper_not_found", L"VOT helper не найден"},
        {L"dialog.vot_helper_exe_was_not_found_at_the_selected_path", L"В выбранном пути не найден vot-helper.exe"},
        {L"dialog.vot_helper_does_not_start", L"VOT helper не запускается"},
        {L"dialog.the_selected_vot_helper_exe_was_found_but_failed_the_lau", L"Выбранный vot-helper.exe найден, но не прошёл проверку запуска. Выберите другой файл или установите VOT helper заново."},
        {L"dialog.ffmpeg_selected", L"FFmpeg указан"},
        {L"dialog.ffmpeg_found_and_will_be_used_to_merge_video_audio_track", L"FFmpeg найден и будет использоваться для объединения видео/аудио дорожек и переконвертации.\n\nПуть:\n"},
        {L"dialog.ffmpeg_not_found_without_it_the_application_can_download", L"FFmpeg не найден. Без него приложение сможет скачивать только готовые единые файлы без переконвертации и объединения отдельных видео/аудио дорожек."},
        {L"dialog.whisper_cpp_ready", L"Whisper.cpp готов"},
        {L"dialog.whisper_model_required", L"Нужна модель Whisper"},
        {L"dialog.whisper_cpp_not_found", L"Whisper.cpp не найден"},
        {L"dialog.whisper_cli_exe_found_and_can_be_used_for_local_transcri", L"whisper-cli.exe найден и может использоваться для локальной транскрибации."},
        {L"dialog.whisper_cpp_is_required_for_local_transcription_without", L"Whisper.cpp нужен для локальной транскрибации без VOT."},
        {L"dialog.model", L"\n\nМодель:\n"},
        {L"dialog.not_found_you_can_download_the_recommended_model", L"Не найдена. Можно скачать рекомендуемую модель."},
        {L"dialog.vot_helper_ready", L"VOT helper готов"},
        {L"dialog.vot_helper_exe_found_and_will_be_used_for_translation_an", L"vot-helper.exe найден и будет использоваться для перевода и VOT-транскрибации.\n\nПуть:\n"},
        {L"dialog.vot_helper_exe_is_required_for_voice_over_translation_an", L"vot-helper.exe нужен для Voice Over Translation и VOT-транскрибации.\n\nМожно установить его автоматически или выбрать папку с готовым vot-helper.exe."},
        {L"dialog.custom", L"Свой"},
        {L"dialog.missing", L"Не хватает: "},
        {L"dialog.install_it_or_set_the_path_in_tools", L". Установите или укажите путь в Инструментах."},
        {L"dialog.recommended", L" · рекоменд."},
        {L"dialog.best_quality", L" · лучшее качество"},
        {L"dialog.the_following_files_will_be_overwritten_or_changed", L"Будут перезаписаны или изменены следующие файлы:\n\n"},
        {L"dialog.downloads", L"Загрузки"},
        {L"dialog.transcription", L"Транскрибация"},
        {L"dialog.translation", L"Перевод"},
        {L"dialog.additional", L"Дополнительно"},
        {L"dialog.tools", L"Инструменты"},
        {L"dialog.about", L"О программе"},
        {L"dialog.auto_check_on", L"Автопроверка: Вкл"},
        {L"dialog.auto_check_off", L"Автопроверка: Выкл"},
        {L"dialog.hide", L"Скрыть"},
        {L"dialog.details", L"Подробно"},
        {L"dialog.open_tools", L"Открыть Инструменты"},
        {L"dialog.error_the_text_can_be_copied_for_diagnostics", L"Ошибка. Текст можно скопировать для диагностики."},
        {L"dialog.application_update_is_available", L"Доступно обновление приложения."},
        {L"dialog.application_information", L"Информация приложения."},
        {L"dialog.ready", L"Готов"},
        {L"dialog.no", L"Нет"},
        {L"dialog.model_2", L"Модель"},
        {L"dialog.no_model", L"Нет модели"},
        {L"dialog.model_3", L"Модель:"},
        {L"dialog.install_whisper_cpp_then_choose_or_download_a_recognitio", L"Установите Whisper.cpp, затем выберите или скачайте модель распознавания."},
        {L"dialog.whisper_model", L"Модель Whisper"},
        {L"dialog.choose_the_recognition_model_larger_models_are_more_accu", L"Выберите модель распознавания. Большие модели точнее, компактные быстрее скачиваются и работают."},
        {L"dialog.selected", L"Выбрано: "},
        {L"dialog.several_vot_helper_exe_files_were_found_in_the_selected", L"В выбранной папке найдено несколько vot-helper.exe. Укажите, какой использовать."},
        {L"dialog.select_the_needed_lines_or_copy_the_whole_current_log", L"Выделите нужные строки или скопируйте весь текущий лог."},
        {L"dialog.engine_subtitles_and_vot_subtitle_translation", L"Движок, субтитры и перевод VOT-субтитров."},
        {L"dialog.voice_over_target_language_and_ffmpeg_integration", L"Целевой язык озвучки и FFmpeg-интеграция перевода."},
        {L"dialog.application_language_and_updates", L"Язык приложения и обновления."},
        {L"dialog.yt_dlp_ffmpeg_whisper_cpp_and_voice_over_translation_sta", L"Статус yt-dlp, FFmpeg, Whisper.cpp и Voice Over Translation."},
        {L"dialog.application_version_and_updates", L"Версия приложения и обновления."},
        {L"dialog.quality_container_and_new_task_behavior", L"Качество, контейнер и поведение новых задач."},
        {L"dialog.quality", L"Качество"},
        {L"dialog.default_quality_for_new_downloads", L"Качество по умолчанию для новых загрузок."},
        {L"dialog.container", L"Контейнер"},
        {L"dialog.final_file_container_without_changing_naming", L"Формат итогового файла без изменения схемы имен."},
        {L"dialog.parallelism", L"Параллельность"},
        {L"dialog.how_many_tasks_can_download_at_the_same_time", L"Сколько задач можно скачивать одновременно."},
        {L"dialog.parallel_downloads", L"Параллельные загрузки"},
        {L"dialog.application_language", L"Язык приложения"},
        {L"dialog.interface_language_applies_after_restart", L"Язык интерфейса применяется сразу после выбора."},
        {L"dialog.language_will_apply_after_restart", L"Язык будет применён сразу после сохранения."},
        {L"dialog.update_checks", L"Проверка обновлений"},
        {L"dialog.automatically_check_for_new_versions_on_startup", L"Автоматически проверять новые версии при запуске."},
        {L"dialog.engine", L"Движок"},
        {L"dialog.whisper_cpp_or_vot_helper_exe_for_txt_srt_creation", L"Whisper.cpp или vot-helper.exe для создания TXT/SRT."},
        {L"dialog.subtitles", L"Субтитры"},
        {L"dialog.ffmpeg_modes_are_available", L"FFmpeg-режимы доступны."},
        {L"dialog.ffmpeg_modes_are_visible_but_require_ffmpeg", L"FFmpeg-режимы видимы, но требуют установленный FFmpeg."},
        {L"dialog.vot_subtitle_translation", L"Перевод VOT-субтитров"},
        {L"dialog.used_only_with_the_vot_engine", L"Используется только при движке VOT."},
        {L"dialog.language_and_integration", L"Язык и интеграция"},
        {L"dialog.mp3_is_always_created_ffmpeg_can_embed_or_mix_the_voice", L"MP3 создается всегда; FFmpeg может встроить или смешать озвучку."},
        {L"dialog.mp3_is_always_created_video_modes_require_ffmpeg", L"MP3 создается всегда; режимы видео требуют FFmpeg."},
        {L"dialog.original_volume_while_mixing", L"Громкость оригинала при смешивании"},
        {L"dialog.main_downloader_found", L"Основной загрузчик найден."},
        {L"dialog.main_downloader_not_found", L"Основной загрузчик не найден."},
        {L"dialog.version", L"Версия"},
        {L"dialog.path", L"Путь:"},
        {L"dialog.ready_for_containers_subtitles_and_audio_tracks", L"Готов для контейнеров, субтитров и аудиодорожек."},
        {L"dialog.not_found_or_path_unavailable", L"Не найден или путь недоступен."},
        {L"dialog.status", L"Статус:"},
        {L"dialog.whisper_cli_exe_found", L"whisper-cli.exe найден."},
        {L"dialog.required_for_local_transcription", L"Нужен для локальной транскрибации."},
        {L"dialog.vot_helper_exe_found", L"vot-helper.exe найден."},
        {L"dialog.vot_helper_exe_not_found", L"vot-helper.exe не найден."},
        {L"dialog.portable_win32_downloader_with_yt_dlp_ffmpeg_whisper_cpp", L"Портативный Win32-загрузчик с yt-dlp, FFmpeg, Whisper.cpp и VOT."},
        {L"dialog.version_2", L"Версия: "},
        {L"dialog.later", L"Позже"},
        {L"dialog.install", L"Установить"},
        {L"dialog.closes_the_window_without_continuing", L"Закрывает окно без продолжения."},
        {L"dialog.runs_the_main_action_in_this_window", L"Выполняет основное действие этого окна."},
        {L"dialog.check_for_update", L"Проверить обновление"},
        {L"dialog.checks_for_a_new_application_version", L"Проверяет наличие новой версии приложения."},
        {L"dialog.copy", L"Скопировать"},
        {L"dialog.copies_this_window_text_to_the_clipboard", L"Копирует текст этого окна в буфер обмена."},
        {L"dialog.closes_the_window", L"Закрывает окно."},
        {L"dialog.copy_all", L"Скопировать всё"},
        {L"dialog.copies_the_whole_current_log_to_the_clipboard", L"Копирует весь текущий лог в буфер обмена."},
        {L"dialog.closes_the_log_window", L"Закрывает окно логов."},
        {L"dialog.choose_folder", L"Выбрать папку"},
        {L"dialog.skip", L"Пропустить"},
        {L"dialog.downloads_and_configures_local_ffmpeg_for_merging_video", L"Скачивает и настраивает локальный FFmpeg для объединения видео и аудио."},
        {L"dialog.closes_the_window_without_configuring_ffmpeg", L"Закрывает окно без настройки FFmpeg."},
        {L"dialog.choose_the_folder_containing_ffmpeg_exe_or_the_parent_fo", L"Выберите папку, где находится ffmpeg.exe, или папку выше, содержащую bin\\ffmpeg.exe, ffprobe.exe и ffplay.exe."},
        {L"dialog.choose_model", L"Выбрать модель"},
        {L"dialog.downloads_and_configures_whisper_cpp_gpu_version_when_av", L"Скачивает и настраивает Whisper.cpp: GPU-версию при доступности, иначе CPU."},
        {L"dialog.opens_whisper_model_selection_download_the_selected_mode", L"Открывает выбор модели Whisper: скачать выбранную или указать папку с готовой моделью."},
        {L"dialog.choose_the_folder_containing_whisper_cli_exe", L"Выберите папку, где находится whisper-cli.exe."},
        {L"dialog.closes_the_window_without_changes", L"Закрывает окно без изменений."},
        {L"dialog.downloads_and_configures_vot_helper_exe", L"Скачивает и настраивает vot-helper.exe."},
        {L"dialog.choose_the_folder_containing_vot_helper_exe", L"Выберите папку, где находится vot-helper.exe."},
        {L"dialog.download_selected", L"Скачать выбранную"},
        {L"dialog.choose_folder_2", L"Указать папку"},
        {L"dialog.cancel", L"Отмена"},
        {L"dialog.downloads_the_selected_whisper_model", L"Скачивает выбранную модель Whisper."},
        {L"dialog.choose_the_folder_with_already_downloaded_whisper_models", L"Выберите папку с уже скачанными моделями Whisper."},
        {L"dialog.closes_the_window_without_downloading", L"Закрывает окно без скачивания."},
        {L"dialog.select", L"Выбрать"},
        {L"dialog.use_the_selected_vot_helper_exe", L"Использовать выбранный vot-helper.exe."},
        {L"dialog.closes_the_window_without_choosing", L"Закрывает окно без выбора."},
        {L"dialog.unknown_ffmpeg_installation_error", L"Неизвестная ошибка установки FFmpeg"},
        {L"dialog.cuda_whisper_is_installed_but_failed_the_launch_check_sw", L"CUDA Whisper установлен, но не прошёл проверку запуска. Переключено на установленный CPU backend."},
        {L"dialog.unknown_whisper_cpp_installation_error", L"Неизвестная ошибка установки Whisper.cpp"},
        {L"dialog.unknown_whisper_model_download_error", L"Неизвестная ошибка скачивания модели Whisper"},
        {L"dialog.unknown_vot_helper_installation_error", L"Неизвестная ошибка установки VOT helper"},
        {L"dialog.downloading_update", L"Скачивание обновления..."},
        {L"dialog.unknown_application_update_error", L"Неизвестная ошибка обновления приложения"},
        {L"dialog.cancels_the_current_operation", L"Отменяет текущую операцию."},
        {L"dialog.max", L"Макс."},
        {L"dialog.selects_the_quality_used_for_new_tasks", L"Выбирает качество, которое будет использоваться для новых задач."},
        {L"dialog.selects_the_final_file_container_for_new_tasks", L"Выбирает контейнер итогового файла для новых задач."},
        {L"dialog.off", L"Выкл"},
        {L"dialog.check_for_updates", L"Проверить обновления"},
        {L"dialog.save", L"Сохранить"},
        {L"dialog.collapses_or_expands_the_side_navigation", L"Сворачивает или разворачивает боковую навигацию."},
        {L"dialog.opens_download_settings", L"Открывает настройки загрузок."},
        {L"dialog.opens_transcription_settings", L"Открывает настройки транскрибации."},
        {L"dialog.opens_translation_settings", L"Открывает настройки перевода."},
        {L"dialog.opens_additional_application_settings", L"Открывает дополнительные настройки приложения."},
        {L"dialog.opens_tool_status_and_setup", L"Открывает статус и настройку инструментов."},
        {L"dialog.opens_application_information", L"Открывает информацию о приложении."},
        {L"dialog.selects_the_interface_language_after_application_restart", L"Выбирает язык интерфейса приложения."},
        {L"dialog.opens_ffmpeg_setup_and_the_existing_installation_flow", L"Открывает настройку FFmpeg и существующий поток установки."},
        {L"dialog.use_whisper_cli_exe_for_transcription", L"Использовать whisper-cli.exe для транскрибации."},
        {L"dialog.use_vot_helper_exe_subtitles_to_get_srt_txt", L"Использовать vot-helper.exe subtitles для получения SRT/TXT."},
        {L"dialog.target_language_for_vot_subtitles", L"Целевой язык VOT-субтитров."},
        {L"dialog.opens_whisper_cpp_model_and_folder_setup", L"Открывает настройку Whisper.cpp, модели и выбора папки."},
        {L"dialog.opens_vot_helper_exe_setup", L"Открывает настройку vot-helper.exe."},
        {L"dialog.save_txt_srt_next_to_the_video_without_changing_the_vide", L"Сохранять TXT/SRT рядом с видео без изменения видеофайла."},
        {L"dialog.add_srt_as_a_separate_subtitle_track", L"Добавлять SRT как отдельную дорожку субтитров."},
        {L"dialog.burn_subtitles_into_the_video_image", L"Выжигать субтитры в изображение."},
        {L"dialog.target_language_for_vot_translation", L"Целевой язык перевода VOT."},
        {L"dialog.save_translation_as_a_separate_mp3_next_to_the_video", L"Сохранять перевод отдельным MP3 рядом с видео."},
        {L"dialog.add_translation_as_a_separate_audio_track", L"Добавлять перевод как отдельную аудиодорожку."},
        {L"dialog.mix_translation_with_the_original_audio_track", L"Смешивать перевод с оригинальной аудиодорожкой."},
        {L"dialog.decreases_the_original_track_volume_while_mixing", L"Уменьшает громкость оригинальной дорожки при смешивании."},
        {L"dialog.increases_the_original_track_volume_while_mixing", L"Увеличивает громкость оригинальной дорожки при смешивании."},
        {L"dialog.goes_to_tool_selection_and_installation", L"Переходит к выбору и установке инструментов."},
        {L"dialog.shows_or_hides_the_yt_dlp_path", L"Показывает или скрывает путь yt-dlp."},
        {L"dialog.shows_or_hides_the_ffmpeg_path", L"Показывает или скрывает путь FFmpeg."},
        {L"dialog.shows_or_hides_whisper_cpp_and_model_paths", L"Показывает или скрывает пути Whisper.cpp и модели."},
        {L"dialog.shows_or_hides_the_vot_helper_exe_path", L"Показывает или скрывает путь vot-helper.exe."},
        {L"dialog.enables_or_disables_automatic_application_update_checks", L"Включает или отключает автоматическую проверку обновлений приложения."},
        {L"dialog.decreases_the_number_of_parallel_downloads", L"Уменьшает количество параллельных загрузок."},
        {L"dialog.increases_the_number_of_parallel_downloads", L"Увеличивает количество параллельных загрузок."},
        {L"dialog.closes_settings_without_saving_changes", L"Закрывает настройки без сохранения изменений."},
        {L"dialog.saves_the_selected_settings", L"Сохраняет выбранные настройки."},
        {L"dialog.choose_the_folder_with_whisper_models", L"Выберите папку с моделями Whisper"},
        {L"dialog.whisper_model_not_found", L"Модель Whisper не найдена"},
        {L"dialog.no_known_whisper_models_were_found_in_the_selected_folde", L"В выбранной папке и её подпапке models не найдены известные модели Whisper."},
        {L"dialog.updates", L"Обновления"},
        {L"dialog.application_path_is_unavailable", L"Путь приложения недоступен."},
        {L"dialog.choose_the_whisper_folder", L"Выберите папку Whisper"},
        {L"dialog.choose_the_vot_helper_folder", L"Выберите папку VOT helper"},
        {L"dialog.canceling", L"Отмена..."},
        {L"dialog.update_downloaded_the_application_will_close_and_restart", L"Обновление скачано. Приложение будет закрыто и запущено заново."},
        {L"dialog.whisper_cpp_installed", L"Whisper.cpp установлен."},
        {L"dialog.whisper_cpp_installed_now_download_a_model_the_model_win", L"Whisper.cpp установлен. Теперь скачайте модель; далее откроется окно моделей."},
        {L"dialog.whisper_model_downloaded", L"Модель Whisper скачана."},
        {L"dialog.vot_helper_installed", L"VOT helper установлен."},
        {L"dialog.ffmpeg_installed", L"FFmpeg установлен."},
        {L"dialog.failed_to_update_the_application", L"Не удалось обновить приложение."},
        {L"dialog.failed_to_download_the_whisper_model", L"Не удалось скачать модель Whisper."},
        {L"dialog.failed_to_install_vot_helper", L"Не удалось установить VOT helper."},
        {L"dialog.failed_to_install_whisper_cpp", L"Не удалось установить Whisper.cpp."},
        {L"dialog.failed_to_install_ffmpeg", L"Не удалось установить FFmpeg."},
        {L"dialog.models", L"Модели"},
        {L"dialog.copy_2", L"Копировать"},
        {L"dialog.a_tool_required_for_this_action_is_missing", L"Не хватает инструмента для выбранного действия."},
        {L"dialog.confirm_overwrite_before_starting_the_operation", L"Подтвердите перезапись перед запуском операции."},
        {L"dialog.overwrite", L"Перезаписать"},
        {L"dialog.log_is_empty", L"Лог пока пуст."},
        {L"dialog.portable_win32_video_downloader_for_youtube", L"Портативный Win32 загрузчик видео с YouTube.\n\n"},
        {L"dialog.installing_ffmpeg", L"Установка FFmpeg"},
        {L"dialog.preparing", L"Подготовка..."},
        {L"dialog.installing_whisper_cpp", L"Установка Whisper.cpp"},
        {L"dialog.downloading_whisper_model", L"Скачивание модели Whisper"},
        {L"dialog.installing_vot_helper", L"Установка VOT helper"},
        {L"dialog.application_update", L"Обновление приложения"},
        {L"dialog.youtubedownloader_exe_was_not_found_in_the_latest_releas", L"В последнем релизе не найден файл YoutubeDownloader.exe."},
        {L"dialog.current_version_installed", L"Установлена актуальная версия: "},
        {L"dialog.update_available", L"Обновление доступно"},
        {L"dialog.update_check_failed", L"Проверка обновлений не удалась"},
        {L"dialog.unknown_error", L"Неизвестная ошибка."},
        {L"actions.transcribe", L"Транскрибировать"},
        {L"actions.translate", L"Перевести"},
        {L"actions.retry", L"Повторить"},
        {L"actions.clear", L"Очистить"},
        {L"actions.tool_is_not_ready", L"Инструмент не готов"},
        {L"actions.whisper_transcription_requires_ffmpeg_the_application_mu", L"Для транскрибации через Whisper требуется FFmpeg: приложению нужно извлечь аудиодорожку перед запуском whisper-cli.exe.\n\n"},
        {L"actions.open_tools_to_install_ffmpeg_or_choose_the_folder_with_f", L"Откройте раздел Инструменты, чтобы установить FFmpeg или выбрать папку с ffmpeg.exe."},
        {L"actions.whisper_cli_exe_for_local_transcription_was_not_found", L"Не найден whisper-cli.exe для локальной транскрибации.\n\n"},
        {L"actions.open_tools_to_choose_the_whisper_cpp_folder_or_install_t", L"Откройте раздел Инструменты, чтобы выбрать папку Whisper.cpp или установить инструмент."},
        {L"actions.the_selected_whisper_model_was_not_found_without_a_model", L"Не найдена выбранная модель Whisper. Без модели whisper-cli.exe не сможет распознать аудио.\n\n"},
        {L"actions.open_tools_to_download_or_choose_a_whisper_model", L"Откройте раздел Инструменты, чтобы скачать или выбрать модель Whisper."},
        {L"actions.whisper_transcription_requires_several_components_ffmpeg", L"Для транскрибации через Whisper нужно подготовить несколько компонентов: FFmpeg для извлечения аудио, "},
        {L"actions.whisper_cli_exe_for_recognition_and_the_whisper_model", L"whisper-cli.exe для распознавания и модель Whisper.\n\n"},
        {L"actions.open_tools_to_install_or_choose_the_missing_files", L"Откройте раздел Инструменты, чтобы установить или выбрать недостающие файлы."},
        {L"actions.the_cuda_whisper_backend_is_selected_in_settings_but_cud", L"В настройках выбран CUDA-backend Whisper, но CUDA недоступна или приложение нашло только CPU-версию whisper-cli.exe.\n\n"},
        {L"actions.open_tools_to_install_the_cuda_version_choose_cpu_mode_o", L"Откройте раздел Инструменты, чтобы установить CUDA-версию, выбрать CPU-режим или скачать подходящий Whisper заново."},
        {L"actions.vot_helper_exe_for_voice_over_translation_was_not_found", L"Не найден vot-helper.exe для Voice Over Translation.\n\n"},
        {L"actions.open_tools_to_choose_the_vot_folder_or_install_the_tool", L"Откройте раздел Инструменты, чтобы выбрать папку VOT или установить инструмент."},
        {L"actions.audio_track", L"Аудиодорожка"},
        {L"actions.mix", L"Смешать"},
        {L"actions.subtitle_track", L"Дорожка субтитров"},
        {L"actions.burn_into_video", L"Вшить в видео"},
        {L"actions.open_folder", L"Открыть папку"},
        {L"actions.configure", L"Настроить"},
        {L"actions.no_cuda", L"CUDA нет"},
        {L"actions.selected", L"Выбран"},
        {L"actions.reinstall_cuda", L"Переустановить CUDA"},
        {L"actions.install_cuda", L"Установить CUDA"},
        {L"actions.reinstall_cpu", L"Переустановить CPU"},
        {L"actions.install_cpu", L"Установить CPU"},
        {L"actions.source_media_file_is_no_longer_available", L"Исходный медиафайл больше недоступен"},
        {L"actions.ffmpeg_is_no_longer_available", L"FFmpeg больше недоступен"},
        {L"actions.whisper_cli_exe_is_no_longer_available", L"whisper-cli.exe больше недоступен"},
        {L"actions.whisper_model_is_no_longer_available", L"Модель Whisper больше недоступна"},
        {L"actions.vot_helper_exe_is_no_longer_available", L"vot-helper.exe больше недоступен"},
        {L"actions.vot_helper_is_installed_but_failed_the_launch_check", L"VOT helper установлен, но не прошёл проверку запуска"},
        {L"actions.whisper_cpp_is_installed_but_failed_the_launch_check", L"Whisper.cpp установлен, но не прошёл проверку запуска"},
        {L"actions.a_new_output_conflict_appeared_repeat_the_operation_and", L"Появился новый конфликт вывода. Повторите операцию и подтвердите перезапись."},
        {L"actions.waiting_for_transcription", L"Ожидает транскрибации..."},
        {L"actions.waiting_for_translation", L"Ожидает перевода..."},
        {L"actions.waiting_for_processing", L"Ожидает обработки..."},
        {L"actions.cut", L"Вырезать"},
        {L"actions.select_all", L"Выделить всё"},
        {L"backend.d", L" д"},
        {L"backend.h", L" ч"},
        {L"backend.min", L" мин"},
        {L"backend.s", L" с"},
        {L"download_queue.stopped", L"Остановлено"},
        {L"download_queue.loading", L"Загрузка"},
        {L"tools.ffmpeg_found", L"FFmpeg найден"},
        {L"tools.ffmpeg_path_is_not_set", L"Путь FFmpeg не задан"},
        {L"tools.ffmpeg_exe_was_not_found_at_the_selected_path", L"В выбранном пути не найден ffmpeg.exe"},
        {L"tools.downloading_ffmpeg", L"Скачивание FFmpeg..."},
        {L"tools.extracting_ffmpeg", L"Распаковка FFmpeg..."},
        {L"tools.installing_ffmpeg", L"Установка FFmpeg..."},
        {L"tools.very_fast_low_quality_75_mib", L"очень быстро / низкое качество / 75 МиБ"},
        {L"tools.minimal_model_for_weak_pcs_and_quick_drafts", L"Минимальная модель для слабых ПК и быстрых черновиков."},
        {L"tools.fast_basic_quality_142_mib", L"быстро / базовое качество / 142 МиБ"},
        {L"tools.small_model_when_speed_matters_most", L"Небольшая модель, когда важнее всего скорость."},
        {L"tools.balanced_better_than_base_466_mib", L"баланс / лучше Base / 466 МиБ"},
        {L"tools.good_compromise_for_short_videos", L"Хороший компромисс для коротких видео."},
        {L"tools.higher_quality_slower_1_5_gib", L"выше качество / медленнее / 1.5 ГиБ"},
        {L"tools.noticeably_better_than_small_but_slower", L"Заметно лучше Small, но работает медленнее."},
        {L"tools.recommended_fast_high_quality_1_5_gib", L"рекомендуется / быстро / высокое качество / 1.5 ГиБ"},
        {L"tools.practical_default_close_to_large_quality_and_faster", L"Практичный вариант по умолчанию: близко к Large по качеству и быстрее."},
        {L"tools.best_quality_slower_2_9_gib", L"лучшее качество / медленнее / 2.9 ГиБ"},
        {L"tools.best_recognition_quality_but_the_heaviest_regular_model", L"Лучшее качество распознавания, но самая тяжелая обычная модель."},
        {L"tools.smaller_faster_lower_quality", L"меньше / быстрее / ниже качество"},
        {L"tools.compressed_small_for_speed_and_space_saving", L"Сжатая Small для скорости и экономии места."},
        {L"tools.smaller_fast_medium_lower_quality", L"меньше / быстрый Medium / ниже качество"},
        {L"tools.compressed_medium_when_small_quality_is_not_enough", L"Сжатая Medium, когда качества Small недостаточно."},
        {L"tools.compact_fast_good_quality", L"компактно / быстро / хорошее качество"},
        {L"tools.compressed_turbo_with_moderate_quality_loss", L"Сжатая Turbo с умеренной потерей качества."},
        {L"tools.compact_large_high_quality_slower", L"компактная Large / высокое качество / медленнее"},
        {L"tools.compressed_large_v3_without_the_full_2_9_gib_size", L"Сжатая Large v3 без полного размера 2.9 ГиБ."},
        {L"tools.balanced_compression_better_than_q5", L"сбалансированное сжатие / лучше q5"},
        {L"tools.compressed_turbo_with_less_quality_loss_than_q5", L"Сжатая Turbo с меньшей потерей качества, чем q5."},
        {L"tools.downloading_whisper_cpp", L"Скачивание whisper.cpp..."},
        {L"tools.extracting_whisper_cpp", L"Распаковка whisper.cpp..."},
        {L"tools.installing_whisper_cpp", L"Установка whisper.cpp..."},
        {L"tools.downloading_model", L"Скачивание модели "},
        {L"tools.vot_helper_found", L"VOT helper найден"},
        {L"tools.downloading_vot_helper", L"Скачивание VOT helper..."},
        {L"tools.extracting_vot_helper", L"Распаковка VOT helper..."},
        {L"tools.installing_vot_helper", L"Установка VOT helper..."},
        {L"tools.new_version_available", L"Доступна новая версия: "},
        {L"tools.current_version", L"\nТекущая версия: "},
        {L"tools.download_and_install_the_update_now", L"\n\nСкачать и установить обновление сейчас?\n"},
        {L"tools.the_application_will_close_and_restart", L"Приложение будет закрыто и запущено заново."},
        {L"transcription.file_for_recognition_not_found", L"Файл для распознавания не найден"},
        {L"transcription.whisper_cli_exe_not_found", L"whisper-cli.exe не найден"},
        {L"transcription.vot_helper_exe_not_found", L"vot-helper.exe не найден"},
        {L"transcription.video_url_is_unavailable", L"Ссылка на видео недоступна"},
        {L"transcription.failed_to_create_the_temporary_audio_folder", L"Не удалось создать папку для временного аудио"},
        {L"transcription.extracting_audio_for_recognition", L"Извлечение аудио для распознавания"},
        {L"transcription.failed_to_extract_audio", L"Не удалось извлечь аудио: "},
        {L"transcription.ffmpeg_exited_with_an_error", L"FFmpeg завершился с ошибкой"},
        {L"transcription.recognizing_speech", L"Распознавание речи"},
        {L"transcription.whisper_exited_with_an_error", L"Whisper завершился с ошибкой: "},
        {L"transcription.text", L"неизвестная ошибка"},
        {L"transcription.whisper_did_not_save_txt_and_srt_transcript_files", L"Whisper не сохранил TXT и SRT файлы расшифровки"},
        {L"transcription.getting_vot_subtitles", L"Получение субтитров VOT"},
        {L"transcription.refining_vot_subtitle_language", L"Уточнение языка субтитров VOT"},
        {L"transcription.vot_helper_exe_did_not_get_subtitles", L"vot-helper.exe не получил субтитры: "},
        {L"transcription.vot_helper_exe_did_not_create_an_srt_file", L"vot-helper.exe не создал SRT-файл"},
        {L"transcription.saving_txt_and_srt", L"Сохранение TXT и SRT"},
        {L"transcription.failed_to_create_txt_from_vot_subtitles", L"Не удалось создать TXT из субтитров VOT"},
        {L"transcription.failed_to_save_vot_txt_and_srt_files", L"Не удалось сохранить TXT и SRT файлы VOT"},
        {L"transcription.embedding_subtitles_into_video", L"Встраивание субтитров в видео"},
        {L"transcription.ffmpeg_did_not_embed_subtitles_into_video", L"FFmpeg не встроил субтитры в видео: "},
        {L"transcription.failed_to_replace_the_source_video_with_the_subtitled_ve", L"Не удалось заменить исходное видео версией с субтитрами"},
        {L"transcription.saving_transcript", L"Сохранение расшифровки"},
        {L"voiceover.file_for_translation_not_found", L"Файл для перевода не найден"},
        {L"voiceover.failed_to_create_the_temporary_voice_over_folder", L"Не удалось создать папку для временной озвучки"},
        {L"voiceover.getting_translation", L"Получение перевода"},
        {L"voiceover.vot_helper_exe_exited_with_an_error", L"vot-helper.exe завершился с ошибкой: "},
        {L"voiceover.vot_helper_exe_did_not_create_the_translated_mp3", L"vot-helper.exe не создал mp3 с переводом"},
        {L"voiceover.embedding_translation_into_video", L"Встраивание перевода в видео"},
        {L"voiceover.ffmpeg_did_not_embed_translation_into_video", L"FFmpeg не встроил перевод в видео: "},
        {L"voiceover.failed_to_replace_the_source_video_with_the_translated_v", L"Не удалось заменить исходное видео версией с переводом"},
        {L"voiceover.saving_translation", L"Сохранение перевода"},
        {L"ytdlp.download_completed_part", L"Загрузка завершена (часть)"},
        {L"ytdlp.downloading_video", L"Скачивание видео:"},
        {L"ytdlp.downloading_audio", L"Скачивание аудио:"},
        {L"ytdlp.downloading", L"Скачивание:"}
    };
}
bool HasCyrillic(std::wstring_view text) {
    return std::any_of(text.begin(), text.end(), [](wchar_t ch) {
        return (ch >= L'А' && ch <= L'я') || ch == L'Ё' || ch == L'ё';
    });
}

void ReplaceAll(std::wstring& text, const std::wstring& from, const std::wstring& to) {
    if (from.empty() || from == to) {
        return;
    }

    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

bool ReadLanguageFile(const std::filesystem::path& path, UiLanguage* language, std::unordered_map<std::wstring, std::wstring>* strings) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    try {
        const nlohmann::json root = nlohmann::json::parse(in, nullptr, true, true);
        if (!root.is_object()) {
            return false;
        }
        if (root.contains("version") && (!root["version"].is_number_integer() || root["version"].get<int>() != 1)) {
            return false;
        }
        if (!root.contains("id") || !root["id"].is_string() ||
            !root.contains("name") || !root["name"].is_string() ||
            !root.contains("strings") || !root["strings"].is_object()) {
            return false;
        }

        const std::wstring id = NormalizeLanguageId(Utf8ToWide(root["id"].get<std::string>()));
        const std::wstring name = Utf8ToWide(root["name"].get<std::string>());
        if (id.empty() || name.empty() || id == L"ru") {
            return false;
        }

        std::unordered_map<std::wstring, std::wstring> loaded;
        for (const auto& item : root["strings"].items()) {
            if (!item.value().is_string()) {
                return false;
            }
            const std::wstring key = Utf8ToWide(item.key());
            if (!HasCyrillic(key)) {
                loaded[key] = Utf8ToWide(item.value().get<std::string>());
            }
        }

        if (language) {
            language->id = id;
            language->name = name;
            language->path = path;
        }
        if (strings) {
            *strings = std::move(loaded);
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

std::mutex g_activeLocalizationMutex;
Localization g_activeLocalization;

Localization::Localization()
    : m_strings(RussianStrings()) {
}

std::vector<UiLanguage> Localization::AvailableLanguages(const AppPaths& paths) {
    std::vector<UiLanguage> languages = {{L"ru", L"Русский", {}}};
    std::set<std::wstring> seen = {L"ru"};

    std::error_code ec;
    if (!std::filesystem::is_directory(paths.languagesDir(), ec)) {
        return languages;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(paths.languagesDir(), ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || entry.path().extension() != L".json") {
            continue;
        }
        UiLanguage language;
        if (!ReadLanguageFile(entry.path(), &language, nullptr) || seen.contains(language.id)) {
            continue;
        }
        seen.insert(language.id);
        languages.push_back(std::move(language));
    }

    return languages;
}

Localization Localization::Load(const AppPaths& paths, const std::wstring& languageId) {
    Localization localization;
    localization.m_strings = RussianStrings();

    const std::wstring wanted = NormalizeLanguageId(languageId.empty() ? L"ru" : languageId);
    if (wanted.empty() || wanted == L"ru") {
        return localization;
    }

    for (const UiLanguage& language : AvailableLanguages(paths)) {
        if (language.id != wanted || language.path.empty()) {
            continue;
        }

        std::unordered_map<std::wstring, std::wstring> external;
        if (!ReadLanguageFile(language.path, nullptr, &external)) {
            return localization;
        }
        for (auto& [key, value] : external) {
            localization.m_strings[std::move(key)] = std::move(value);
        }
        localization.m_currentLanguageId = wanted;
        return localization;
    }

    return localization;
}

const std::wstring& Localization::currentLanguageId() const {
    return m_currentLanguageId;
}

void Localization::SetActive(Localization localization) {
    std::lock_guard lock(g_activeLocalizationMutex);
    g_activeLocalization = std::move(localization);
}

std::wstring Localization::UiText(std::wstring_view text) {
    std::lock_guard lock(g_activeLocalizationMutex);
    return g_activeLocalization.Text(text);
}

std::wstring Localization::Text(std::wstring_view key) const {
    const auto it = m_strings.find(std::wstring(key));
    if (it != m_strings.end()) {
        return it->second;
    }
    if (key.find(L'.') == std::wstring_view::npos) {
        return std::wstring(key);
    }

    std::wstring translated(key);
    std::vector<const std::pair<const std::wstring, std::wstring>*> fragments;
    fragments.reserve(m_strings.size());
    for (const auto& item : m_strings) {
        if (item.first.size() >= 4) {
            fragments.push_back(&item);
        }
    }
    std::sort(fragments.begin(), fragments.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->first.size() > rhs->first.size();
    });
    for (const auto* item : fragments) {
        ReplaceAll(translated, item->first, item->second);
    }
    return translated;
}

std::wstring Localization::Format(std::wstring_view key, const std::unordered_map<std::wstring, std::wstring>& values) const {
    std::wstring text = Text(key);
    for (const auto& [name, value] : values) {
        const std::wstring needle = L"{" + name + L"}";
        size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::wstring::npos) {
            text.replace(pos, needle.size(), value);
            pos += value.size();
        }
    }
    return text;
}
