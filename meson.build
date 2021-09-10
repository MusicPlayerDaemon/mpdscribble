project('mpdscribble', 'cpp',
  version: '0.24',
  meson_version: '>= 0.47',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
  ],
  license: 'GPLv2+',
)

is_linux = host_machine.system() == 'linux'
is_windows = host_machine.system() == 'windows'

compiler = meson.get_compiler('cpp')

conf = configuration_data()
conf.set_quoted('PACKAGE', meson.project_name())
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('FILE_CONF', join_paths(get_option('prefix'), get_option('sysconfdir'), 'mpdscribble.conf'))

if not get_option('syslog').disabled()
  if compiler.has_function('syslog')
    conf.set('HAVE_SYSLOG', true)
  elif get_option('syslog').enabled()
    error('syslog() not found')
  endif
endif

thread_dep = dependency('threads')
libmpdclient_dep = dependency('libmpdclient', version: '>= 2.5')
libgcrypt_dep = dependency('libgcrypt')

if host_machine.system() == 'linux'
  libsystemd_dep = dependency('libsystemd', required: get_option('systemd'))
  conf.set('HAVE_LIBSYSTEMD', libsystemd_dep.found())
else
  libsystemd_dep = dependency('', required: false)
endif

common_cflags = [
]

test_global_common_flags = [
  '-fvisibility=hidden',
]

test_common_flags = [
  '-Wshadow',
  '-Wpointer-arith',
  '-Wcast-qual',
  '-Wcast-align',
  '-Wwrite-strings',
  '-Wmissing-declarations', '-Wmissing-noreturn', '-Wmissing-format-attribute',
  '-Wredundant-decls', '-Wundef',

  # clang specific warning options:
  '-Wunreachable-code-aggressive',
  '-Wused-but-marked-unused',
]

test_global_cxxflags = test_global_common_flags + [
]

test_cxxflags = test_common_flags + [
  '-fno-threadsafe-statics',
  '-fmerge-all-constants',

  '-Wcomma-subscript',
  '-Wextra-semi',
  '-Wmismatched-tags',
  '-Woverloaded-virtual',
  '-Wsign-promo',
  '-Wvolatile',
  '-Wvirtual-inheritance',

  # a vtable without a dtor is just fine
  '-Wno-non-virtual-dtor',

  # clang specific warning options:
  '-Wcomma',
  '-Wheader-hygiene',
  '-Winconsistent-missing-destructor-override',
]

test_ldflags = [
]

if get_option('buildtype') != 'debug'
  test_global_cxxflags += [
    '-ffunction-sections',
    '-fdata-sections',
  ]
  test_ldflags += [
    '-Wl,--gc-sections',
  ]
endif

add_global_arguments(compiler.get_supported_arguments(test_global_cxxflags), language: 'cpp')
add_project_arguments(compiler.get_supported_arguments(test_cxxflags), language: 'cpp')
add_project_link_arguments(compiler.get_supported_link_arguments(test_ldflags), language: 'cpp')

configure_file(output: 'config.h', configuration: conf)

inc = include_directories(
  'src',

  # for the generated config.h
  '.',
)

subdir('src/util')
subdir('src/io')
subdir('src/system')
subdir('src/net')
subdir('src/event')
subdir('src/lib/curl')

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
  'src/IniFile.cxx',
  'src/Journal.cxx',
  'src/MpdObserver.cxx',
  'src/Log.cxx',

  include_directories: inc,
  dependencies: [
    util_dep,
    event_dep,
    thread_dep,
    libmpdclient_dep,
    libgcrypt_dep,
    curl_dep,
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
