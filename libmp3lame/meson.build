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
  'vector/dsp_scalar.c',
  'vbrquantize.c',
  'version.c',
)

libmp3lame_compile_args = []
libmp3lame_deps = [libm]
libmp3lame_link_whole = []

if simd_x86_sse2
  libmp3lame_sources += files('vector/dsp_x86_sse2.c')
endif

if simd_aarch64_neon
  libmp3lame_sources += files('vector/dsp_aarch64_neon.c')
endif

if mpg123_dep.found()
  libmp3lame_deps += mpg123_dep
endif

foreach arg : ['-fno-semantic-interposition', '-fno-math-errno']
  if cc.has_argument(arg)
    libmp3lame_compile_args += arg
  endif
endforeach

# Do not add -ffast-math implicitly.  MP3 encoder decisions are sensitive to
# FP reassociation, contraction and special-value assumptions; scalar/SIMD
# comparisons must run under the same conservative FP semantics unless the
# caller explicitly opts into different CFLAGS outside this build file.

if simd_x86_avx2
  libmp3lame_avx2 = static_library(
    'mp3lame_avx2',
    files('vector/dsp_x86_avx2.c'),
    include_directories : core_includes,
    c_args : libmp3lame_compile_args + ['-mavx2'],
    dependencies : libmp3lame_deps,
    pic : true,
  )
  libmp3lame_link_whole += libmp3lame_avx2
endif

libmp3lame = library(
  'mp3lame',
  libmp3lame_sources,
  include_directories : core_includes,
  c_args : libmp3lame_compile_args,
  dependencies : libmp3lame_deps,
  link_whole : libmp3lame_link_whole,
  install : true,
  soversion : '0',
)
