gcrypt_dep = dependency('libgcrypt')

gcrypt = static_library(
  'gcrypt',
  'MD5.cxx',
  include_directories: inc,
  dependencies: [
    gcrypt_dep,
  ],
)

gcrypt_dep = declare_dependency(
  link_with: gcrypt,
  dependencies: [
    gcrypt_dep,
  ],
)
