# UI languages

Put extra interface translations in this folder as `*.json`.

Format:

```json
{
  "version": 1,
  "id": "en",
  "name": "English",
  "strings": {
    "dialog.application_language": "Application language"
  }
}
```

Rules:

- Save files as UTF-8.
- `id` must use only `a-z`, `0-9`, `_`, `-`, up to 32 characters.
- `strings` keys must use the flat application keys from the built-in catalog.
- Missing strings fall back to built-in Russian.
- Legacy Russian text keys are ignored.
- Broken files and duplicate ids are ignored.
- Keep translations close to the Russian text length; the current Win32 UI does not auto-resize every control.
