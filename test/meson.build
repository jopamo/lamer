python = import('python').find_installation('python3')

lametest = join_paths(meson.project_source_root(), 'test', 'lametest.py')
testcase_wav = join_paths(meson.project_source_root(), 'testcase.wav')
short_cbrabr = join_paths(meson.project_source_root(), 'test', 'shortCBRABR.op')
short_vbr = join_paths(meson.project_source_root(), 'test', 'shortVBR.op')

test(
  'lame-encode-fixture',
  python,
  args : [lametest, 'encode', '--lame', lame_exe.full_path(), '--input', testcase_wav],
  workdir : meson.current_build_dir(),
)

test(
  'lame-stdin-stdout',
  python,
  args : [lametest, 'stream', '--lame', lame_exe.full_path(), '--input', testcase_wav],
  workdir : meson.current_build_dir(),
)

test(
  'lame-short-cbrabr',
  python,
  args : [lametest, 'matrix', '--lame', lame_exe.full_path(), '--input', testcase_wav, '--options', short_cbrabr],
  workdir : meson.current_build_dir(),
)

test(
  'lame-short-vbr',
  python,
  args : [lametest, 'matrix', '--lame', lame_exe.full_path(), '--input', testcase_wav, '--options', short_vbr],
  workdir : meson.current_build_dir(),
)
