# using include_type:system to work around -Wfloat-equal
libfmt = dependency('fmt', version: '>= 9',
                    include_type: 'system',
                    fallback: ['fmt', 'fmt_dep'])

if compiler.get_id() == 'gcc' and compiler.version().version_compare('>=13') and compiler.version().version_compare('<15')
  libfmt = declare_dependency(
    dependencies: libfmt,
    # suppress bogus GCC 13 warnings: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=109717
    compile_args: ['-Wno-array-bounds', '-Wno-stringop-overflow']
  )
endif

fmt = static_library(
  'fmt',
  'ExceptionFormatter.cxx',
  'RuntimeError.cxx',
  'SystemError.cxx',
  include_directories: inc,
  dependencies: libfmt,
)

fmt_dep = declare_dependency(
  link_with: fmt,
  dependencies: libfmt,
)
