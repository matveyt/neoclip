project('neoclip', 'c', license : 'Unlicense', default_options :
  ['b_ndebug=if-release', 'buildtype=release', 'c_std=c99', 'default_library=shared',
    'install_umask=0177', 'strip=true', 'warning_level=3', 'werror=true'])

install_dir = meson.source_root() / '../lua'
lua = dependency('luajit')
c_args = []
name_suffix = []

if host_machine.system() == 'windows'
  target = 'neoclip_w32'
  sources = ['neoclip_w32.c', 'neo_common.c']
  deps = lua
elif host_machine.system() == 'darwin'
  add_languages('objc')
  target = 'neoclip_mac'
  sources = ['neoclip_mac.m', 'neo_common.c']
  deps = [lua, dependency('AppKit', method : 'extraframework')]
  name_suffix = 'so'
else # *nix
  target = 'neoclip_x11'
  sources = ['neoclip_nix.c', 'neo_x11.c', 'neo_common.c']
  c_args += '-DPLATFORM=X11'
  threads = dependency('threads')
  deps = [lua, meson.get_compiler('c').find_library('X11'), threads]

  # Wayland
  wl_client = dependency('wayland-client', required : false)
  wl_scanner = find_program('wayland-scanner', required : false, native : true)
  if wl_client.found() and wl_scanner.found()
    target2 = 'neoclip_wl'
    wlr_data_control_xml = meson.source_root() / 'extra/wlr-data-control-unstable-v1.xml'
    wlr_data_control_h = custom_target('wlr-data-control-h',
      input : wlr_data_control_xml, output : 'wayland-wlr-data-control-client-protocol.h',
      command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
    wlr_data_control_c = custom_target('wlr-data-control-c',
      input : wlr_data_control_xml, output : 'wayland-wlr-data-control-protocol.c',
      command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
    sources2 = ['neoclip_nix.c', 'neo_wayland.c', 'neo_common.c', wlr_data_control_h,
      wlr_data_control_c]
  endif
endif

library(target, sources, c_args : c_args, dependencies : deps,
  gnu_symbol_visibility : 'internal', name_prefix : '', name_suffix : name_suffix,
  install : true, install_dir : install_dir)

if is_variable('target2')
  library(target2, sources2, c_args : '-DPLATFORM=Wayland',
    dependencies : [lua, wl_client, threads], gnu_symbol_visibility : 'internal',
    name_prefix : '', install : true, install_dir : install_dir)
endif
