project('mpdscribble', 'c',
  version: '0.23',
  meson_version: '>= 0.47',
  default_options: [
    'c_std=c99'
  ],
  license: 'GPLv2+',
)

cc = meson.get_compiler('c')

conf = configuration_data()
conf.set_quoted('PACKAGE', meson.project_name())
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('FILE_CONF', join_paths(get_option('prefix'), get_option('sysconfdir'), 'mpdscribble.conf'))

if not get_option('syslog').disabled()
  if cc.has_function('syslog')
    conf.set('HAVE_SYSLOG', true)
  elif get_option('syslog').enabled()
    error('syslog() not found')
  endif
endif

glib_dep = dependency('glib-2.0', version: '>= 2.6')
libmpdclient_dep = dependency('libmpdclient', version: '>= 2.2')
libgcrypt_dep = dependency('libgcrypt')
libcurl_dep = dependency('libcurl')

test_cflags = [
  '-Wall',
  '-Wextra',
  '-Wno-deprecated-declarations',
  '-Wmissing-prototypes',
  '-Wshadow',
  '-Wpointer-arith',
  '-Wstrict-prototypes',
  '-Wcast-qual',
  '-Wwrite-strings',
]

add_global_arguments(cc.get_supported_arguments(test_cflags), language: 'c')

configure_file(output: 'config.h', configuration: conf)

inc = include_directories(
  'src',

  # for the generated config.h
  '.',
)

executable(
  'mpdscribble',

  'src/mpdscribble.c',
  'src/daemon.c',
  'src/record.c',
  'src/scrobbler.c',
  'src/http_client.c',
  'src/cmdline.c',
  'src/file.c',
  'src/journal.c',
  'src/lmc.c',
  'src/log.c',

  include_directories: inc,
  dependencies: [
    glib_dep,
    libmpdclient_dep,
    libgcrypt_dep,
    libcurl_dep,
  ],
  install: true
)

install_data(
  'AUTHORS', 'COPYING', 'NEWS', 'README.rst',
  install_dir: join_paths(get_option('datadir'), 'doc', meson.project_name()),
)

subdir('doc')

if get_option('test')
  subdir('test')
endif
