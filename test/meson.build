executable(
  'RunHttpClient',

  'RunHttpClient.cxx',
  '../src/HttpClient.cxx',
  '../src/Config.cxx',
  '../src/util/PrintException.cxx',

  include_directories: inc,
  dependencies: [
    glib_dep,
    libcurl_dep,
  ],
)
