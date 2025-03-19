project('myproject', 'c')

exe = executable(
  'symlinks',
  'symlinks.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'symlinks.8',
  install_dir: join_paths(get_option('mandir'), 'man8')
)
