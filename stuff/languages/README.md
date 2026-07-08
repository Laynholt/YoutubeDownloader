# UI languages

Put extra interface translations in this folder as `*.json`.

Format:

```json
{
  "version": 1,
  "id": "en",
  "name": "English",
  "strings": {
    "settings.language.title": "Interface language"
  }
}
```

Rules:

- Save files as UTF-8.
- `id` must use only `a-z`, `0-9`, `_`, `-`, up to 32 characters.
- Missing strings fall back to built-in Russian.
- Broken files and duplicate ids are ignored.
- Keep translations close to the Russian text length; the current Win32 UI does not auto-resize every control.
