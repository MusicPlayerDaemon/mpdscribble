#curl_dep = dependency('libcurl', version: '>= 7.55')

cmake = import('cmake')

curl_options = cmake.subproject_options()
curl_options.add_cmake_defines({
  'BUILD_CURL_EXE': false,
  'BUILD_SHARED_LIBS': false,
  'CURL_DISABLE_LDAP': true,
  'CURL_DISABLE_TELNET': true,
  'CURL_DISABLE_DICT': true,
  'CURL_DISABLE_FILE': true,
  'CURL_DISABLE_FTP': true,
  'CURL_DISABLE_TFTP': true,
  'CURL_DISABLE_LDAPS': true,
  'CURL_DISABLE_RTSP': true,
  'CURL_DISABLE_PROXY': true,
  'CURL_DISABLE_POP3': true,
  'CURL_DISABLE_IMAP': true,
  'CURL_DISABLE_SMTP': true,
  'CURL_DISABLE_GOPHER': true,
  'CURL_DISABLE_COOKIES': true,
  'CURL_DISABLE_CRYPTO_AUTH': true,
  'CURL_DISABLE_ALTSVC': true,
  'CMAKE_USE_LIBSSH2': false,
  'CURL_WINDOWS_SSPI': false,
  'CURL_DISABLE_NTLM': true,
  'BUILD_TESTING': false,
})

if host_machine.system() == 'windows'
  curl_options.add_cmake_defines({
    'CURL_USE_SCHANNEL': true,
    'CURL_USE_OPENSSL': false,
  })
endif

curl_proj = cmake.subproject('curl-8.0.1', options: curl_options)
curl_dep = curl_proj.dependency('libcurl')

curl = static_library(
  'curl',
  'Init.cxx',
  'Global.cxx',
  'Request.cxx',
  include_directories: inc,
  dependencies: [
    curl_dep,
    event_dep,
  ],
)

curl_dep = declare_dependency(
  link_with: curl,
  dependencies: [
    curl_dep,
  ],
)
