project(
  'lamer',
  'c',
  version : '3.101.3',
  meson_version : '>=0.60.0',
  default_options : ['c_std=gnu99', 'warning_level=1'],
)

cc = meson.get_compiler('c')
mpg123_dep = dependency('libmpg123', required : false)
if not mpg123_dep.found()
  mpg123_dep = dependency('mpg123', required : false)
endif

add_project_arguments(
  [
    '-D_GNU_SOURCE',
    '-D_FILE_OFFSET_BITS=64',
    '-DHAVE_CONFIG_H',
  ],
  language : 'c',
)

conf = configuration_data()
conf.set('STDC_HEADERS', true)
conf.set('HAVE_ERRNO_H', cc.has_header('errno.h'))
conf.set('HAVE_FCNTL_H', cc.has_header('fcntl.h'))
conf.set('HAVE_INTTYPES_H', cc.has_header('inttypes.h'))
conf.set('HAVE_LANGINFO_H', false)
conf.set('HAVE_LIMITS_H', cc.has_header('limits.h'))
conf.set('HAVE_MEMCPY', cc.has_function('memcpy', prefix : '#include <string.h>'))
conf.set('HAVE_MPG123', mpg123_dep.found())
conf.set('HAVE_STDINT_H', cc.has_header('stdint.h'))
conf.set('HAVE_STRCHR', cc.has_function('strchr', prefix : '#include <string.h>'))
conf.set('HAVE_UNISTD_H', cc.has_header('unistd.h'))
conf.set('HAVE_ICONV', false)
conf.set('USE_FAST_LOG', true)

configure_file(
  input : 'config.h.meson',
  output : 'config.h',
  configuration : conf,
)

core_includes = include_directories(
  '.',
  'include',
  'libmp3lame',
  'frontend',
)

libm = cc.find_library('m', required : false)

subdir('libmp3lame')
subdir('frontend')

install_headers('include/lame.h')
install_data(
  ['README.md', 'CHANGELOG.md', 'COMPATIBILITY.md'],
  install_dir : join_paths(get_option('datadir'), 'doc', meson.project_name()),
)

subdir('test')
