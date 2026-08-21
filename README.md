# lamer

lamer is a fork of LAME intended to remain a drop-in replacement.

Compatibility goals:
- executable name stays `lame`
- shared library stays `libmp3lame.so.0`
- installed public header stays `lame.h`
- public API keeps the `lame_*` surface
- encoded stream tag identity stays LAME-compatible

Build:
- `meson setup build`
- `meson compile -C build`
- `meson test -C build --print-errorlogs`
