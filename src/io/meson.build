io = static_library(
  'io',
  'BufferedReader.cxx',
  'FileDescriptor.cxx',
  'FileReader.cxx',
  'Open.cxx',
  'Reader.cxx',
  include_directories: inc,
  dependencies: [
    fmt_dep,
  ],
)

io_dep = declare_dependency(
  link_with: io,
)
