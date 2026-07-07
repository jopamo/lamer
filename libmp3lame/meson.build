libmp3lame_sources = files(
  'VbrTag.c',
  'bitstream.c',
  'encoder.c',
  'fft.c',
  'gain_analysis.c',
  'id3tag.c',
  'lame.c',
  'mpglib_interface.c',
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
  dependencies : [libm],
  install : true,
  soversion : '0',
)
