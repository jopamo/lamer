frontend_sources = files(
  'brhist.c',
  'console.c',
  'get_audio.c',
  'lame_main.c',
  'lametime.c',
  'main.c',
  'parse.c',
  'path.c',
  'timestatus.c',
  'usage.c',
)

lame_exe = executable(
  'lame',
  frontend_sources,
  include_directories : core_includes,
  link_with : libmp3lame,
  dependencies : [libm],
  install : true,
)
