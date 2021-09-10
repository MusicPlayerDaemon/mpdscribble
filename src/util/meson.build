util = static_library(
  'util',
  'Exception.cxx',
  'OptionParser.cxx',
  'PrintException.cxx',
  'StringStrip.cxx',
  'StringView.cxx',
  include_directories: inc,
)

util_dep = declare_dependency(
  link_with: util,
)
