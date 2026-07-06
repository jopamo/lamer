# Coverage TODO

**Current:** 74.2% lines (10088/13601), 79.8% functions (631/791), 57.7% branches (4684/8112)  
**Target:** 80%+ lines in core encoder (`libmp3lame/`)

## P0 — Low effort, high impact ✅

- [x] **`tables.c`** — test `lame_get_bitrate()` and `lame_get_samplerate()` with valid/invalid indices
- [x] **`version.c`** — test `get_lame_very_short_version()`, `get_psy_version()`, `get_lame_version_numerical()`
- [x] **`util.c`** — 81% covered (up from 77%) via indirect encoder paths; `calloc_aligned()`/`free_aligned()` blocked: internal header only
- [ ] **`encoder.c`** — error return paths (line 378, bit type edge case at 177) — not easily triggered via public API
- [x] **`id3tag.c`** — fixed UBSan signed left-shift UB in `frame_id_matches()` (line 161)

## P1 — Core encode API paths ✅

- [x] **`lame.c` alternate entry points** — all 11 `lame_encode_buffer_*` variants tested (added `long` and `interleaved_int`)
- [x] **`lame.c` flush paths** — `lame_encode_flush_nogap()` (empty stream + after encode), `lame_encode_flush()`, `lame_close()`
- [x] **`quantize.c` old VBR** — `vbr_rh` mode with mono (44100Hz + 22050Hz) and stereo encode exercises old iteration loop (81% up from 66%)
- [x] **`encoder.c` `vbr_rh` path** — line 528-530 dispatches to `VBR_old_iteration_loop` covered (87% up from 85%)
- [x] **`lame.c` highpass/lowpass filter config** — tested via `lame_set_lowpassfreq`/`lame_set_highpassfreq`
- [x] **`lame.c` `lame_init_params` error returns** — NULL gfp tested; samplerate doesn't cause failure (silent fallback)

## P2 — ID3 tag and VBR header round-trips

- [x] **`id3tag.c` metadata setters** — tested `id3tag_set_title()`, `_artist()`, `_album()`, `_year()`, `_comment()`, `_track()`, `_genre()` + `id3tag_add_v2()` with `lame_get_id3v2_tag()` verify (non-empty tag)
- [x] **`id3tag.c` version control** — tested `id3tag_v1_only()` with `lame_get_id3v1_tag()` (128-byte tag)
- [x] **`VbrTag.c` Xing header** — tested `lame_get_lametag_frame()` after VBR encode + flush (non-empty)
- [x] **`id3tag.c` album art** — tested `id3tag_set_albumart()` with minimal JPEG bytes, verified via `lame_get_id3v2_tag()`
- [x] **`id3tag.c` padding/version control** — tested `id3tag_v2_only()` + `id3tag_set_pad(256)`, `id3tag_add_v2_4_UTF8()` (tag non-empty)
- [x] **`id3tag.c` genre list** — tested `id3tag_genre_list()` with callback (counted entries)
- [x] **`id3tag.c` custom frames** — tested `id3tag_set_fieldvalue()`, `id3tag_set_textinfo_latin1()`, `id3tag_set_comment_latin1()` (tag non-empty)
- [x] **`id3tag.c` multi-encoding setters** — `id3tag_set_textinfo_utf16()`, `id3tag_set_comment_utf16()`, `id3tag_set_fieldvalue_utf16()`, `id3tag_set_textinfo_utf8()`, `id3tag_set_comment_utf8()` tested (57% up from 46%)
- [x] **`id3tag.c` remaining** — `id3tag_pad_v2()`, `id3tag_space_v1()`, `id3tag_v2_4_UTF8_only()`, `id3tag_init()` all tested (60% up from 57%)

## P3 — Set/get API completion

- [x] **`set_get.c` preset/tune** — `lame_set_preset()` with 128/STANDARD/EXTREME tested (presets.c 86% up from 81%)
- [x] **`set_get.c` VBR parameters** — `lame_set_VBR_q()`, `lame_set_VBR_min_bitrate_kbps()`, `lame_set_VBR_max_bitrate_kbps()`, `lame_set_VBR_hard_min()`
- [x] **`set_get.c` ATH parameters** — `lame_set_noATH()`, `lame_set_ATHonly()`, `lame_set_ATHshort()`, `lame_set_ATHtype()`
- [x] **`set_get.c` filter parameters** — `lame_set_lowpassfreq()`, `lame_set_highpassfreq()`, `lame_set_lowpasswidth()`, `lame_set_highpasswidth()`
- [x] **`set_get.c` MS/stereo** — `lame_set_force_ms()`
- [x] **`set_get.c` ReplayGain** — `lame_set_findReplayGain()`, `lame_set_decode_on_the_fly()`
- [x] **`set_get.c` emphasis** — `lame_set_emphasis()`
- [x] **`set_get.c` scale left/right** — `lame_set_scale_left()`, `lame_set_scale_right()`
- [x] **`set_get.c` short block control** — `lame_set_force_short_blocks()`, `lame_set_no_short_blocks()`
- [x] **`set_get.c` misc** — `lame_set_disable_reservoir()`, `lame_set_strict_ISO()`, `lame_set_error_protection()`, `lame_set_scale()`, `lame_set_copyright()`, `lame_set_original()`, `lame_set_extension()`
- [x] **`set_get.c` info queries** — `lame_get_framesize()`, `lame_get_out_samplerate()`, `lame_get_compression_ratio()`, `lame_get_num_channels()`, `lame_get_brate()`, `lame_get_VBR()`, `lame_get_mode()`, `lame_get_quality()`, `lame_get_in_samplerate()`, `lame_get_version()`, `lame_get_encoder_delay()`, `lame_get_encoder_padding()`, `lame_get_totalframes()`, `lame_get_frameNum()`, `lame_get_size_mp3buffer()`
- [ ] **`set_get.c` preset variants** — behind `DEPRECATED_OR_OBSOLETE_CODE_REMOVED`: `lame_set_preset_notune()`, `lame_set_preset_expopts()`, `lame_set_tune()` — only in `#else` block
- [ ] **`set_get.c` ATH remaining** — `lame_set_athaa_type()` (public); `lame_set_athaa_loudapprox()` behind deprecated guard
- [ ] **`set_get.c` padding_type** — behind `DEPRECATED_OR_OBSOLETE_CODE_REMOVED`
- [ ] **`set_get.c` mode_automs** — behind `DEPRECATED_OR_OBSOLETE_CODE_REMOVED`
- [ ] **`set_get.c` findPeakSample** — behind `DEPRECATED_OR_OBSOLETE_CODE_REMOVED`

## P4 — Error handling and edge cases

- [x] **`lame.c` histogram queries** — all 5 histogram functions tested with real encode data
- [x] **`lame.c` `lame_print_config` / `lame_print_internals`** — called before and after encode (output to stderr)
- [ ] **`lame.c` allocation failures** — mock malloc failure to exercise cleanup paths (lines 1644-1654, 2478-2501)
- [x] **`lame.c` `lame_encode_buffer` template error paths** — nsamples=0, NULL buffer tested; dual flush returns 0
- [x] **`lame.c` flush edge cases** — ID3v1 writing in flush with write_id3tag_automatic=1 tested
- [ ] **`util.c` logging callbacks** — `lame_report_fnc()`, `lame_report_def()` are internal (not in public API)
- [ ] **`VbrTag.c` read path** — parse an existing MP3 Xing header via `GetVbrTag()` and `IsVbrTag()` (internal)
- [ ] **`psymodel.c` edge cases** — lines 317-326, 458-460, 709-711, 878-885 (sfb partition edge cases)
- [ ] **`quantize_pvt.c` ATH bypass** — `ATH->useAdjust != 0` paths (lines 292-302)
- [ ] **`takehiro.c` accumulator overflow** — lines 243-323 with large quantization values
- [ ] **`vbrquantize.c` scalefac selection edge cases** — lines 716-751, 833-895
- [x] **`gain_analysis.c` rare sample rates** — all 9 freqindex paths tested (48000..8000 Hz) (96% up from 94%)
- [x] **`lame.c` init_params edge cases** — samplerate=0, VBR_q=99, out_samplerate=0, 8000 Hz encode all succeed
- [x] **`VbrTag.c` multiple VBR modes** — vbr_mtrh, vbr_abr, vbr_default all produce lametag frames
- [ ] **`reservoir.c` `ResvMax` edge cases** — lines 152, 160-161

## P5 — Nice to have

- [x] **`mpglib_interface.c`** — hip init/exit/reporting stubs + all 6 decode variants exercised (49% up from 0%); fixed ASan leak in `hip_decode_init_gapless`
- [x] **`set_get.c` ATH remaining** — `lame_set_athaa_type()` + `lame_get_athaa_type()`, `lame_set_athaa_sensitivity()`, `lame_set_asm_optimizations()`
- [x] **`lame.c` free_format** — encode with `lame_set_free_format(1)`, verify `lame_bitrate_kbps` free-format path
- [x] **`id3tag.c` edge cases** — track number bounds (0 → -1, 256 → -1, 1 → 0), year/empty comment, id3tag_set_track returned values
- [ ] **`id3tag.c` custom frame (`set_frame_custom2`)** — set and retrieve arbitrary ID3 frames
- [ ] **`frontend/get_audio.c` WAV reading** — test parse `parse_file_header()` and `open_wave_file()` with various WAV formats
- [ ] **`frontend/parse.c` argument parsing** — test `parse_args()` with various flag combinations to exercise uncovered branches
- [x] **`lame.c` `lame_mp3_tags_fid()`** — writes VBR Xing header to temp file (VbrTag.c 73% up from 68%)
- [x] **`lame.c` `lame_init_bitstream()`** — called explicitly before flush (no crash)
- [x] **`lame.c` `lame_bitrate_kbps()`** — retrieves bitrate table array
- [x] **`version.c` `get_lame_os_bitness()`** — returns non-empty string
- [x] **`util.c`** — 85% up from 82%; (calloc_aligned/free_aligned still internal only)
