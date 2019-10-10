executable(
  'run_http_client',

  'run_http_client.c',
  '../src/http_client.c',
  '../src/file.c',

  include_directories: inc,
  dependencies: [
    glib_dep,
    libcurl_dep,
  ],
)
