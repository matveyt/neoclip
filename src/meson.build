project('neoclip', 'c', default_options : \
  ['buildtype=release', 'c_std=c11', 'default_library=shared', \
  'strip=true', 'warning_level=3', 'werror=true'])

library('neoclip_w32', ['neoclip_w32.c', 'splitjoin.c'], \
  name_prefix : '', dependencies : dependency('lua5.1'), \
  install : true, install_dir : meson.source_root() / '../lua')
