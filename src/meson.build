project('neoclip', 'c', license : 'Unlicense', default_options :
  ['b_ndebug=if-release', 'buildtype=release', 'c_std=c99', 'default_library=shared',
    'install_umask=0177', 'strip=true', 'warning_level=3', 'werror=true'])

install_dir = meson.source_root() / '../lua'
lua = dependency('luajit')
name_prefix = ''
name_suffix = []

if host_machine.system() == 'windows'
  neoclip = 'neoclip_w32'
  sources = [neoclip + '.c', 'neo_common.c']
  deps = lua
elif host_machine.system() == 'darwin'
  add_languages('objc')
  neoclip = 'neoclip_mac'
  sources = [neoclip + '.m', 'neo_common.c']
  deps = [lua, dependency('AppKit', method : 'extraframework')]
  name_suffix = 'so'
else # *nix
  neoclip = 'neoclip_x11'
  sources = [neoclip + '.c', 'neo_x.c', 'neo_common.c']
  threads = dependency('threads')
  deps = [lua, meson.get_compiler('c').find_library('X11'), threads]
endif

library(neoclip, sources, dependencies : deps, gnu_symbol_visibility : 'internal',
  name_prefix : name_prefix, name_suffix : name_suffix, install : true,
  install_dir : install_dir)
