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

lame_frontend_deps = [libm]

if mpg123_dep.found()
  lame_frontend_deps += mpg123_dep
endif

lame_exe = executable(
  'lame',
  frontend_sources,
  include_directories : core_includes,
  link_with : libmp3lame,
  dependencies : lame_frontend_deps,
  install : true,
)
