system_sources = []

if host_machine.system() != 'windows'
  system_sources += [
    'EventPipe.cxx',
  ]
endif

if host_machine.system() == 'linux'
  system_sources += [
    'EventFD.cxx',
    'SignalFD.cxx',
    'EpollFD.cxx',
  ]
endif

system = static_library(
  'system',
  system_sources,
  include_directories: inc,
)

if is_windows
  winsock_dep = compiler.find_library('ws2_32')
else
  winsock_dep = dependency('', required: false)
endif

system_dep = declare_dependency(
  link_with: system,
  dependencies: [
    io_dep,
    winsock_dep,
  ],
)
