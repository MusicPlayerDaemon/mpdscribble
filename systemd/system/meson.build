systemd_system_unit_dir = get_option('systemd_system_unit_dir')
if systemd_system_unit_dir == ''
  systemd = dependency('systemd', required: false)
  if systemd.found()
      systemd_system_unit_dir = systemd.get_pkgconfig_variable('systemdsystemunitdir')
  endif
endif
if systemd_system_unit_dir == ''
  systemd_system_unit_dir = join_paths(get_option('prefix'), 'lib', 'systemd', 'system')
endif

configure_file(
  input: 'mpdscribble.service.in',
  output: 'mpdscribble.service',
  configuration: systemd_unit_conf,
  install_dir: systemd_system_unit_dir,
)
