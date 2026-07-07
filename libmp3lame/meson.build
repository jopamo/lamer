libmp3lame_sources = files(
  'VbrTag.c',
  'bitstream.c',
  'encoder.c',
  'fft.c',
  'gain_analysis.c',
  'id3tag.c',
  'hip_decode.c',
  'lame.c',
  'newmdct.c',
  'presets.c',
  'psymodel.c',
  'quantize.c',
  'quantize_pvt.c',
  'reservoir.c',
  'set_get.c',
  'steadyprotect.c',
  'tables.c',
  'takehiro.c',
  'util.c',
  'vbrquantize.c',
  'version.c',
)

libmp3lame_compile_args = []
libmp3lame_deps = [libm]

if mpg123_dep.found()
  libmp3lame_deps += mpg123_dep
endif

foreach arg : ['-fno-semantic-interposition', '-fno-math-errno']
  if cc.has_argument(arg)
    libmp3lame_compile_args += arg
  endif
endforeach

if not get_option('debug') and cc.has_argument('-ffast-math')
  libmp3lame_compile_args += '-ffast-math'
endif

libmp3lame = library(
  'mp3lame',
  libmp3lame_sources,
  include_directories : core_includes,
  c_args : libmp3lame_compile_args,
  dependencies : libmp3lame_deps,
  install : true,
  soversion : '0',
)
