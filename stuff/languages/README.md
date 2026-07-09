# UI languages

Put extra interface translations in this folder as `*.json`. Russian is built into the application. External language files are optional and can be incomplete; missing strings fall back to built-in Russian.

Start from `en.json`, copy it to a new file, then change `id`, `name`, and translated values.

Minimal format:

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
- `id` must be unique and must not be `ru`.
- `name` is shown in the application language selector.
- `strings` keys must use the flat application keys from the built-in catalog.
- Missing strings fall back to built-in Russian.
- Legacy Russian text keys are ignored.
- Broken files and duplicate ids are ignored.
- Keep translations close to the Russian text length; the current Win32 UI does not auto-resize every control.

Key style:

- Keys are flat names such as `app.download`, `dialog.application_language`, or `tools.downloading_ffmpeg`.
- Do not translate keys. Translate only values.
- Values may contain `\n` for line breaks.
- Some values are used as fragments in composed messages, so keep leading/trailing spaces when they exist in the source value.

Install:

1. Put the file in `stuff/languages/`.
2. Start or restart the application if it is already running.
3. Open Settings -> Additional -> Application language.
4. Pick the language and save.
