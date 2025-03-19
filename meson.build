project('myproject', ['c', 'cpp'], version : '1.0.0')

libsymlinks = static_library(
  'symlinks',
  ['symlinks.c'],  # Contains symlinks_main()
  include_directories : include_directories('.')
)

executable(
  'symlinks',
  ['cli_main.c'],
  link_with : libsymlinks,
  install : true,
  install_dir : get_option('bindir')
)

install_data(
  'symlinks.8',
  install_dir : join_paths(get_option('mandir'), 'man8')
)
