project('mpdscribble', 'cpp',
  version: '0.26',
  meson_version: '>= 0.56',
  default_options: [
    'cpp_std=c++2a',
    'warning_level=3',

    'fmt:cpp_std=c++20',

    # If we build those libraries as Meson subproject, they shall be
    # linked statically into the MPD executable.
    'curl:default_library=static',
    'fmt:default_library=static',
    'libmpdclient:default_library=static',

    # disable CURL options we don't need
    'curl:tool=disabled',
    'curl:tests=disabled',
    'curl:unittests=disabled',
    'curl:brotli=disabled',
    'curl:cookies=disabled',
    'curl:progress-meter=disabled',
    'curl:zstd=disabled',
    'curl:kerberos-auth=disabled',
    'curl:negotiate-auth=disabled',
    'curl:gss-api=disabled',
    'curl:ntlm=disabled',
    'curl:ssh=disabled',
    'curl:dict=disabled',
    'curl:file=disabled',
    'curl:ftp=disabled',
    'curl:gopher=disabled',
    'curl:imap=disabled',
    'curl:ldap=disabled',
    'curl:ldaps=disabled',
    'curl:mqtt=disabled',
    'curl:pop3=disabled',
    'curl:rtmp=disabled',
    'curl:rtsp=disabled',
    'curl:smb=disabled',
    'curl:smtp=disabled',
    'curl:telnet=disabled',
    'curl:tftp=disabled',

    # Not interested in compiler warnings from subprojects.
    'curl:werror=false',
    'curl:warning_level=0',
    'fmt:warning_level=0',
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

common_cppflags = [
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
  # make relocations read-only (hardening)
  '-Wl,-z,relro',

  # no lazy binding, please - not worth it for a daemon
  '-Wl,-z,now',
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

if is_windows
  common_cppflags += [
    # enable Windows Vista APIs
    '-DWINVER=0x0600', '-D_WIN32_WINNT=0x0600',

    # enable strict type checking in the Windows API headers
    '-DSTRICT',

    # reduce header bloat by disabling obscure and obsolete Windows
    # APIs
    '-DWIN32_LEAN_AND_MEAN',

    # disable more Windows APIs which are not used by mpdscribble
    '-DNOGDI', '-DNOBITMAP', '-DNOCOMM',
    '-DNOUSER',

    # reduce COM header bloat
    '-DCOM_NO_WINDOWS_H',

    # disable Internet Explorer specific APIs
    '-D_WIN32_IE=0',
  ]
endif

add_project_arguments(common_cppflags, language: 'cpp')

add_global_arguments(compiler.get_supported_arguments(test_global_cxxflags), language: 'cpp')
add_project_arguments(compiler.get_supported_arguments(test_cxxflags), language: 'cpp')
add_project_link_arguments(compiler.get_supported_link_arguments(test_ldflags), language: 'cpp')

thread_dep = dependency('threads')
libmpdclient_dep = dependency('libmpdclient', version: '>= 2.10')

if host_machine.system() == 'linux'
  libsystemd_dep = dependency('libsystemd', required: get_option('systemd'))
  conf.set('HAVE_LIBSYSTEMD', libsystemd_dep.found())
else
  libsystemd_dep = dependency('', required: false)
endif

configure_file(output: 'config.h', configuration: conf)

inc = include_directories(
  'src',

  # for the generated config.h
  '.',
)

subdir('src/lib/fmt')

subdir('src/util')
subdir('src/io')
subdir('src/system')
subdir('src/net')
subdir('src/event')
subdir('src/lib/curl')

if host_machine.system() == 'windows'
  subdir('src/lib/wincrypt')
  md5_dep = wincrypt_dep
else
  subdir('src/lib/gcrypt')
  md5_dep = gcrypt_dep
endif

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
  'src/XdgBaseDirectory.cxx',
  'src/IgnoreList.cxx',

  include_directories: inc,
  dependencies: [
    util_dep,
    event_dep,
    thread_dep,
    libmpdclient_dep,
    md5_dep,
    curl_dep,
    libsystemd_dep,
    fmt_dep,
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
