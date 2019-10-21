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

thread_dep = dependency('threads')
boost_dep = dependency('boost', version: '>= 1.62')
glib_dep = dependency('glib-2.0', version: '>= 2.16')
libmpdclient_dep = dependency('libmpdclient', version: '>= 2.5')
libgcrypt_dep = dependency('libgcrypt')
libcurl_dep = dependency('libcurl', version: '>= 7.18')

if host_machine.system() == 'linux'
  libsystemd_dep = dependency('libsystemd', required: get_option('systemd'))
  conf.set('HAVE_LIBSYSTEMD', libsystemd_dep.found())
else
  libsystemd_dep = dependency('', required: false)
endif

common_cflags = [
  # no header bloat for the iostream library we don't use
  '-DBOOST_NO_IOSTREAM',

  # avoid the runtime dependency on libboost_system
  '-DBOOST_ERROR_CODE_HEADER_ONLY',

  # disable deprecated boost::system features
  '-DBOOST_SYSTEM_NO_DEPRECATED',
]

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

add_global_arguments(common_cflags, language: 'cpp')
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
  'src/Form.cxx',
  'src/CommandLine.cxx',
  'src/ReadConfig.cxx',
  'src/Journal.cxx',
  'src/MpdObserver.cxx',
  'src/Log.cxx',

  'src/lib/curl/Init.cxx',
  'src/lib/curl/Global.cxx',
  'src/lib/curl/Request.cxx',
  'src/lib/curl/Escape.cxx',
  'src/util/Exception.cxx',
  'src/util/OptionParser.cxx',
  'src/util/PrintException.cxx',
  'src/util/StringStrip.cxx',

  include_directories: inc,
  dependencies: [
    thread_dep,
    boost_dep,
    glib_dep,
    libmpdclient_dep,
    libgcrypt_dep,
    libcurl_dep,
    libsystemd_dep,
  ],
  install: true
)

install_data(
  'AUTHORS', 'COPYING', 'NEWS', 'README.rst',
  install_dir: join_paths(get_option('datadir'), 'doc', meson.project_name()),
)

if libsystemd_dep.found()
  subdir('systemd')
endif

subdir('doc')

if get_option('test')
  subdir('test')
endif
