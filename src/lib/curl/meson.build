curl_dep = dependency('libcurl', version: '>= 7.18')

curl = static_library(
  'curl',
  'Init.cxx',
  'Global.cxx',
  'Request.cxx',
  'Escape.cxx',
  include_directories: inc,
  dependencies: [
    curl_dep,
    boost_dep,
  ],
)

curl_dep = declare_dependency(
  link_with: curl,
  dependencies: [
    curl_dep,
  ],
)
