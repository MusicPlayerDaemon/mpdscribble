executable(
  'RunHttpClient',

  'RunHttpClient.cxx',
  '../src/HttpClient.cxx',
  '../src/Config.cxx',
  '../src/lib/curl/Init.cxx',
  '../src/lib/curl/Global.cxx',
  '../src/util/PrintException.cxx',

  include_directories: inc,
  dependencies: [
    thread_dep,
    boost_dep,
    glib_dep,
    libcurl_dep,
  ],
)
