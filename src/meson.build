project('neoclip', 'c', default_options : \
  ['buildtype=release', 'c_std=c11', 'default_library=shared', \
  'strip=true', 'warning_level=3', 'werror=true'])

if host_machine.system() == 'windows'
  neoclip = 'neoclip_w32'
  c_args = []
elif host_machine.system() == 'linux'
  neoclip = 'neoclip_x11'
  c_args = ['-fvisibility=hidden']
endif

library(neoclip, [neoclip + '.c', 'splitjoin.c'], c_args : c_args, \
  name_prefix : '', dependencies : dependency('lua5.1'), \
  install : true, install_dir : meson.source_root() / '../lua')
