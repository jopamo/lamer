# lamer

lamer is a library-only fork of LAME focused on MP3 encoding.

Compatibility goals:
- shared library is published as `libmp3lame.so.1`
- installed public header remains available as `lame.h`
- exported encoder ABI is kept explicit and checked
- encoded stream tag identity stays LAME-compatible

There is no installed command-line frontend or MP3 decoder in this project.
Applications should link against `libmp3lame` and use the public encoder API.
The SONAME bump is intentional: removing the legacy decoder API is an ABI
break from the old drop-in library.

Build:
- `meson setup build`
- `meson compile -C build`
- `meson test -C build --print-errorlogs`

Test-only C consumer:

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

The consumer is never installed and is not part of the production library.

Development hooks:

```sh
git config core.hooksPath .githooks
```

The pre-commit hook formats staged C and header files with `clang-format`
without changing unrelated unstaged edits.
