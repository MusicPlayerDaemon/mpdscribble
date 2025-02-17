util = static_library(
  'util',
  'Exception.cxx',
  'OptionParser.cxx',
  'PrintException.cxx',
  'StringStrip.cxx',
  include_directories: inc,
  dependencies: [
    fmt_dep,
  ],
)

util_dep = declare_dependency(
  link_with: util,
)
