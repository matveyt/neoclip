project('neoclip', 'c', license : 'Unlicense', default_options :
  ['b_ndebug=if-release', 'buildtype=release', 'c_std=c99', 'default_library=shared',
    'install_umask=0177', 'strip=true', 'warning_level=3', 'werror=true'])

lua = dependency('luajit')
name_prefix = ''
name_suffix = []

if host_machine.system() == 'windows'
  neoclip = 'neoclip_w32'
  sources = [neoclip + '.c', 'splitjoin.c']
  deps = lua
elif host_machine.system() == 'darwin'
  add_languages('objc')
  neoclip = 'neoclip_mac'
  sources = [neoclip + '.m', 'splitjoin.c']
  # BUG: luajit.pc wants -pagezero_size that is not supported for libraries
  deps = [lua.partial_dependency(compile_args : true),
    meson.get_compiler('objc').find_library('luajit'),
    dependency('AppKit', method : 'extraframework')]
  name_suffix = 'so'
else # *nix
  neoclip = 'neoclip_x11'
  sources = [neoclip + '.c', 'neo_x.c', 'splitjoin.c']
  deps = [lua, meson.get_compiler('c').find_library('X11'), dependency('threads')]
endif

library(neoclip, sources, dependencies : deps, gnu_symbol_visibility : 'internal',
  name_prefix : name_prefix, name_suffix : name_suffix, install : true,
  install_dir : meson.source_root() / '../lua')
