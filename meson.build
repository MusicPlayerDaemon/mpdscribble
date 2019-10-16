project('mpdscribble', 'cpp',
  version: '0.23',
  meson_version: '>= 0.47',
  default_options: [
    'cpp_std=c++14',
  ],
  license: 'GPLv2+',
)

cc = meson.get_compiler('cpp')

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

boost_dep = dependency('boost', version: '>= 1.62')
glib_dep = dependency('glib-2.0', version: '>= 2.16')
libmpdclient_dep = dependency('libmpdclient', version: '>= 2.5')
libgcrypt_dep = dependency('libgcrypt')
libcurl_dep = dependency('libcurl', verison: '>= 7.18')

test_cflags = [
  '-Wall',
  '-Wextra',
  '-Wno-deprecated-declarations',
  '-Wshadow',
  '-Wpointer-arith',
  '-Wcast-qual',
  '-Wcast-align',
  '-Wwrite-strings',
  '-Wmissing-declarations', '-Wmissing-noreturn', '-Wmissing-format-attribute',
  '-Wredundant-decls', '-Wundef',

  '-Wno-missing-field-initializers',
  '-Wno-non-virtual-dtor',

  '-fvisibility=hidden',
]

test_ldflags = [
]

if get_option('buildtype') != 'debug'
  test_cflags += [
    '-ffunction-sections',
    '-fdata-sections',
  ]
  test_ldflags += [
    '-Wl,--gc-sections',
  ]
endif

add_global_arguments(cc.get_supported_arguments(test_cflags), language: 'cpp')
add_global_link_arguments(cc.get_supported_link_arguments(test_ldflags), language: 'cpp')

configure_file(output: 'config.h', configuration: conf)

inc = include_directories(
  'src',

  # for the generated config.h
  '.',
)

executable(
  'mpdscribble',

  'src/Main.cxx',
  'src/Instance.cxx',
  'src/Daemon.cxx',
  'src/Protocol.cxx',
  'src/Scrobbler.cxx',
  'src/MultiScrobbler.cxx',
  'src/HttpClient.cxx',
  'src/Form.cxx',
  'src/CommandLine.cxx',
  'src/Config.cxx',
  'src/ReadConfig.cxx',
  'src/Journal.cxx',
  'src/MpdObserver.cxx',
  'src/Log.cxx',

  'src/util/PrintException.cxx',

  include_directories: inc,
  dependencies: [
    boost_dep,
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
