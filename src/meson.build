project('neoclip', 'c', default_options : \
  ['buildtype=release', 'c_std=c11', 'default_library=shared', \
  'strip=true', 'warning_level=3', 'werror=true'])

sources = ['splitjoin.c']
c_args = []
deps = [dependency('luajit')]

if host_machine.system() == 'windows'
  neoclip = 'neoclip_w32'
elif host_machine.system() == 'linux'
  neoclip = 'neoclip_x11'
  sources += 'neo_x.c'
  c_args += ['-fPIC', '-fvisibility=internal']
  deps += dependency('threads')
  deps += meson.get_compiler('c').find_library('X11')
endif
  sources += neoclip + '.c'

library(neoclip, sources, c_args : c_args, name_prefix : '', dependencies : deps, \
  install : true, install_dir : meson.source_root() / '../lua')
