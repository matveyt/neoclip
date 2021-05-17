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

  # Wayland support (WIP)
  wl_client = dependency('wayland-client', required : false)
  wl_protocols = dependency('wayland-protocols', required : false)
  wl_scanner = find_program('wayland-scanner', required : false, native : true)
  if false # wl_client.found() and wl_protocols.found() and wl_scanner.found()
    neoclip2 = 'neoclip_wl'
    xdg_datadir = wl_protocols.get_variable(pkgconfig : 'pkgdatadir')
    xdg_shell_xml = xdg_datadir / 'stable/xdg-shell/xdg-shell.xml'
    xdg_shell_h = custom_target('xdg-shell-h',
      input : xdg_shell_xml, output : 'wayland-xdg-shell-client-protocol.h',
      command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
    xdg_shell_c = custom_target('xdg-shell-c',
      input : xdg_shell_xml, output : 'wayland-xdg-shell-protocol.c',
      command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
    sources2 = [neoclip2 + '.c', 'neo_w.c', 'neo_common.c', xdg_shell_h, xdg_shell_c]
    deps2 = [lua, wl_client, meson.get_compiler('c').find_library('rt'), threads]
  endif
endif

library(neoclip, sources, dependencies : deps, gnu_symbol_visibility : 'internal',
  name_prefix : name_prefix, name_suffix : name_suffix, install : true,
  install_dir : install_dir)

if is_variable('neoclip2')
  library(neoclip2, sources2, dependencies : deps2, gnu_symbol_visibility : 'internal',
    name_prefix : name_prefix, name_suffix : name_suffix, install : true,
    install_dir : install_dir)
endif
