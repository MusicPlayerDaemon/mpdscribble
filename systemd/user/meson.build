systemd_user_unit_dir = get_option('systemd_user_unit_dir')
if systemd_user_unit_dir == ''
  systemd = dependency('systemd', required: false)
  if systemd.found()
    systemd_user_unit_dir = systemd.get_pkgconfig_variable('systemduserunitdir')
  endif
endif
if systemd_user_unit_dir == ''
  systemd_user_unit_dir = join_paths(get_option('prefix'), 'lib', 'systemd', 'user')
endif

configure_file(
  input: 'mpdscribble.service.in',
  output: 'mpdscribble.service',
  configuration: systemd_unit_conf,
  install_dir: systemd_user_unit_dir,
)
