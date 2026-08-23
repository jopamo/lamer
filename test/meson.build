abi_test = executable(
  'lame-abi-test',
  files('lame-abi-test.c'),
  include_directories : core_includes,
  link_with : libmp3lame,
  dependencies : [libm],
)

test(
  'lame-abi',
  abi_test,
  workdir : meson.current_build_dir(),
  suite : ['abi'],
)

consumer = executable(
  'lame-test-consumer',
  files('lame-test-consumer.c'),
  include_directories : core_includes,
  link_with : libmp3lame,
  dependencies : [libm],
)

data_dir = join_paths(meson.project_source_root(), 'test', 'data')
consumer_inputs = [
  ['clipping', 'clipping_mix_stereo_44100_16_3s.wav'],
  ['impulse', 'impulse_train_stereo_44100_16_2s.wav'],
  ['noise', 'noise_stereo_44100_16_3s.wav'],
  ['odd-length', 'odd_length_8081f_stereo_44100_16.wav'],
  ['silence-mono', 'silence_mono_44100_16_0.5s.wav'],
  ['silence-stereo-short', 'silence_stereo_44100_16_0.5s.wav'],
  ['silence-stereo', 'silence_stereo_44100_16_2s.wav'],
  ['sine-11khz', 'sine_1000_mono_11025_16_2s.wav'],
  ['sine-48khz', 'sine_1000_mono_48000_16_1s.wav'],
  ['sine-8khz', 'sine_1000_mono_8000_16_2s.wav'],
  ['sine-22khz', 'sine_220_mono_22050_16_2s.wav'],
  ['sine-440-mono', 'sine_440_mono_44100_16_2s.wav'],
  ['sine-440-8bit', 'sine_440_mono_44100_8_2s.wav'],
  ['sine-440-stereo', 'sine_440_stereo_44100_16_2s.wav'],
  ['sine-997hz', 'sine_997hz_stereo_44100_16_5s.wav'],
  ['speech-short', 'speech_female_librispeech_1.5s.wav'],
  ['speech-medium', 'speech_female_librispeech_2.1s.wav'],
  ['speech-long', 'speech_female_librispeech_5.1s.wav'],
  ['sweep-log', 'sweep_log_20hz_20khz_stereo_44100_16_10s.wav'],
  ['sweep-linear', 'swept_sine_stereo_44100_16_3s.wav'],
  ['testcase', 'testcase_stereo_44100_16.wav'],
]

foreach input : consumer_inputs
  label = input[0]
  path = join_paths(data_dir, input[1])
  test(
    f'lame-consumer-cbr-@label@',
    consumer,
    args : [path, join_paths(meson.current_build_dir(), f'lame-test-cbr-@label@.mp3')],
    workdir : meson.current_build_dir(),
    suite : ['consumer'],
  )
  test(
    f'lame-consumer-vbr-@label@',
    consumer,
    args : ['--vbr', path, join_paths(meson.current_build_dir(), f'lame-test-vbr-@label@.mp3')],
    workdir : meson.current_build_dir(),
    suite : ['consumer'],
  )
endforeach
