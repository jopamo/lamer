python = import('python').find_installation('python3')

# ── Paths ─────────────────────────────────────────────────────────────────────
lametest      = join_paths(meson.project_source_root(), 'test', 'lametest.py')
data_dir      = join_paths(meson.project_source_root(), 'test', 'data')
options_dir   = join_paths(meson.project_source_root(), 'test')

lame_exe_path = lame_exe.full_path()

# Option files
short_cbrabr  = join_paths(options_dir, 'shortCBRABR.op')
short_vbr     = join_paths(options_dir, 'shortVBR.op')
full_cbrabr   = join_paths(options_dir, 'CBRABR.op')
full_vbr      = join_paths(options_dir, 'VBR.op')
misc_op       = join_paths(options_dir, 'misc.op')
nores_op      = join_paths(options_dir, 'nores.op')

# ── C API unit tests (direct library calls) ──────────────────────────────────

test_api_exe = executable(
  'test_api',
  files('test_api.c'),
  include_directories : core_includes,
  link_with : libmp3lame,
  dependencies : [libm],
)

test(
  'lame-api',
  test_api_exe,
  workdir : meson.current_build_dir(),
  suite : ['api'],
)

# ── Basic smoke tests ────────────────────────────────────────────────────────

test(
  'lame-encode-fixture',
  python,
  args : [lametest, 'encode', '--lame', lame_exe_path, '--input', join_paths(data_dir, 'testcase_stereo_44100_16.wav')],
  workdir : meson.current_build_dir(),
  suite : ['smoke'],
)

test(
  'lame-stdin-stdout',
  python,
  args : [lametest, 'stream', '--lame', lame_exe_path, '--input', join_paths(data_dir, 'testcase_stereo_44100_16.wav')],
  workdir : meson.current_build_dir(),
  suite : ['smoke'],
)

# ── Short matrix tests (fast, run every time) ────────────────────────────────

test(
  'lame-short-cbrabr',
  python,
  args : [lametest, 'matrix', '--lame', lame_exe_path, '--input', join_paths(data_dir, 'testcase_stereo_44100_16.wav'), '--options', short_cbrabr],
  workdir : meson.current_build_dir(),
  suite : ['matrix'],
)

test(
  'lame-short-vbr',
  python,
  args : [lametest, 'matrix', '--lame', lame_exe_path, '--input', join_paths(data_dir, 'testcase_stereo_44100_16.wav'), '--options', short_vbr],
  workdir : meson.current_build_dir(),
  suite : ['matrix'],
)

# ── Full matrix tests (comprehensive, slower) ────────────────────────────────

test(
  'lame-full-cbrabr',
  python,
  args : [lametest, 'matrix', '--lame', lame_exe_path, '--input', join_paths(data_dir, 'testcase_stereo_44100_16.wav'), '--options', full_cbrabr],
  workdir : meson.current_build_dir(),
  suite : ['matrix', 'full'],
)

test(
  'lame-full-vbr',
  python,
  args : [lametest, 'matrix', '--lame', lame_exe_path, '--input', join_paths(data_dir, 'testcase_stereo_44100_16.wav'), '--options', full_vbr],
  workdir : meson.current_build_dir(),
  suite : ['matrix', 'full'],
)

test(
  'lame-misc',
  python,
  args : [lametest, 'matrix', '--lame', lame_exe_path, '--input', join_paths(data_dir, 'testcase_stereo_44100_16.wav'), '--options', misc_op],
  workdir : meson.current_build_dir(),
  suite : ['matrix', 'full'],
)

test(
  'lame-nores',
  python,
  args : [lametest, 'matrix', '--lame', lame_exe_path, '--input', join_paths(data_dir, 'testcase_stereo_44100_16.wav'), '--options', nores_op],
  workdir : meson.current_build_dir(),
  suite : ['matrix', 'full'],
)

# ── Roundtrip tests (encode→decode, verify properties match) ─────────────────
# Covers: tones, noise, silence, transients, clipping, speech, odd lengths,
# varied sample rates, mono, 8-bit

roundtrip_inputs = [
  ['testcase',         'testcase_stereo_44100_16.wav'],
  ['stereo-440hz',     'sine_440_stereo_44100_16_2s.wav'],
  ['mono-440hz',       'sine_440_mono_44100_16_2s.wav'],
  ['8bit-mono',        'sine_440_mono_44100_8_2s.wav'],
  ['8khz-mono',        'sine_1000_mono_8000_16_2s.wav'],
  ['11khz-mono',       'sine_1000_mono_11025_16_2s.wav'],
  ['22khz-mono',       'sine_220_mono_22050_16_2s.wav'],
  ['48khz-mono',       'sine_1000_mono_48000_16_1s.wav'],
  ['997hz-stereo',     'sine_997hz_stereo_44100_16_5s.wav'],
  ['silence-stereo',   'silence_stereo_44100_16_2s.wav'],
  ['silence-mono',     'silence_mono_44100_16_0.5s.wav'],
  ['silence-stereo-short', 'silence_stereo_44100_16_0.5s.wav'],
  ['noise-stereo',     'noise_stereo_44100_16_3s.wav'],
  ['swept-sine-linear','swept_sine_stereo_44100_16_3s.wav'],
  ['sweep-log',        'sweep_log_20hz_20khz_stereo_44100_16_10s.wav'],
  ['impulse-train',    'impulse_train_stereo_44100_16_2s.wav'],
  ['clipping-mix',     'clipping_mix_stereo_44100_16_3s.wav'],
  ['odd-length',       'odd_length_8081f_stereo_44100_16.wav'],
  ['speech-1.5s',      'speech_female_librispeech_1.5s.wav'],
  ['speech-2.1s',      'speech_female_librispeech_2.1s.wav'],
  ['speech-5.1s',      'speech_female_librispeech_5.1s.wav'],
]

foreach rt : roundtrip_inputs
  label = rt[0]
  fname = rt[1]
  test(
    f'lame-roundtrip-@label@'.format(label),
    python,
    args : [lametest, 'roundtrip', '--lame', lame_exe_path, '--input', join_paths(data_dir, fname)],
    workdir : meson.current_build_dir(),
    suite : ['roundtrip'],
  )
endforeach

# ── Format coverage test (all WAVs in data/ with one setting) ────────────────

test(
  'lame-formats',
  python,
  args : [lametest, 'formats', '--lame', lame_exe_path, '--data-dir', data_dir],
  workdir : meson.current_build_dir(),
  suite : ['formats'],
)

# ── Decode validity tests ────────────────────────────────────────────────────

decode_inputs = [
  ['testcase',         'testcase_stereo_44100_16.wav'],
  ['mono-440hz',       'sine_440_mono_44100_16_2s.wav'],
  ['stereo-440hz',     'sine_440_stereo_44100_16_2s.wav'],
  ['8bit-mono',        'sine_440_mono_44100_8_2s.wav'],
  ['8khz-mono',        'sine_1000_mono_8000_16_2s.wav'],
  ['11khz-mono',       'sine_1000_mono_11025_16_2s.wav'],
  ['22khz-mono',       'sine_220_mono_22050_16_2s.wav'],
  ['48khz-mono',       'sine_1000_mono_48000_16_1s.wav'],
  ['997hz-stereo',     'sine_997hz_stereo_44100_16_5s.wav'],
  ['silence-mono',     'silence_mono_44100_16_0.5s.wav'],
  ['silence-stereo',   'silence_stereo_44100_16_2s.wav'],
  ['silence-stereo-short', 'silence_stereo_44100_16_0.5s.wav'],
  ['noise-stereo',     'noise_stereo_44100_16_3s.wav'],
  ['odd-length',       'odd_length_8081f_stereo_44100_16.wav'],
  ['swept-sine-linear','swept_sine_stereo_44100_16_3s.wav'],
  ['sweep-log',        'sweep_log_20hz_20khz_stereo_44100_16_10s.wav'],
  ['impulse-train',    'impulse_train_stereo_44100_16_2s.wav'],
  ['clipping-mix',     'clipping_mix_stereo_44100_16_3s.wav'],
  ['speech-1.5s',      'speech_female_librispeech_1.5s.wav'],
  ['speech-2.1s',      'speech_female_librispeech_2.1s.wav'],
  ['speech-5.1s',      'speech_female_librispeech_5.1s.wav'],
]

foreach dc : decode_inputs
  label = dc[0]
  fname = dc[1]
  test(
    f'lame-decode-@label@'.format(label),
    python,
    args : [lametest, 'decode', '--lame', lame_exe_path, '--input', join_paths(data_dir, fname)],
    workdir : meson.current_build_dir(),
    suite : ['decode'],
  )
endforeach
