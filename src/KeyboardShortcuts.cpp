#include "KeyboardShortcuts.h"

MainWindowShortcutAction ResolveMainWindowShortcut(bool controlDown, unsigned int virtualKey) {
    if (controlDown && (virtualKey == 'V' || virtualKey == 'v')) {
        return MainWindowShortcutAction::PasteUrl;
    }
    if (!controlDown && virtualKey == '\r') {
        return MainWindowShortcutAction::Download;
    }
    return MainWindowShortcutAction::None;
}
