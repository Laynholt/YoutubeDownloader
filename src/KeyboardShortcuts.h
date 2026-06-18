#pragma once

enum class MainWindowShortcutAction {
    None,
    PasteUrl,
    Download
};

MainWindowShortcutAction ResolveMainWindowShortcut(bool controlDown, unsigned int virtualKey);
