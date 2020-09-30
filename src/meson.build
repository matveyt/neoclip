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
  threads = dependency('threads')
  deps = [lua, threads, meson.get_compiler('c').find_library('X11')]

  # Wayland support (not ready yet)
  wl_scanner = find_program('wayland-scanner', required : false, native : true)
  wl_protocols = dependency('wayland-protocols', required : false)
  if wl_scanner.found() and wl_protocols.found() and false
    neoclip2 = 'neoclip_wl'
    xdg_datadir = wl_protocols.get_variable(pkgconfig : 'pkgdatadir')
    xdg_shell_xml = xdg_datadir / 'stable/xdg-shell/xdg-shell.xml'
    xdg_shell_h = custom_target('xdg-shell-h',
      input : xdg_shell_xml, output : 'xdg-shell-client-protocol.h',
      command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
    xdg_shell_c = custom_target('xdg-shell-c',
      input : xdg_shell_xml, output : 'xdg-shell-protocol.c',
      command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
    sources2 = [neoclip2 + '.c', 'neo_w.c', 'splitjoin.c', xdg_shell_h, xdg_shell_c]
    deps2 = [lua, threads, meson.get_compiler('c').find_library('rt'),
      dependency('wayland-client')]
  endif
endif

library(neoclip, sources, dependencies : deps, gnu_symbol_visibility : 'internal',
  name_prefix : name_prefix, name_suffix : name_suffix, install : true,
  install_dir : meson.source_root() / '../lua')

if is_variable('neoclip2')
  library(neoclip2, sources2, dependencies : deps2, gnu_symbol_visibility : 'internal',
    name_prefix : name_prefix, name_suffix : name_suffix, install : true,
    install_dir : meson.source_root() / '../lua')
endif
