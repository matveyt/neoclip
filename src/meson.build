project('neoclip', 'c', license : 'Unlicense', meson_version: '>=0.56.0',
  default_options : ['b_ndebug=if-release', 'buildtype=release', 'c_std=c99',
    'install_umask=0177', 'prefix=' + meson.project_source_root() / '..', 'strip=true',
    'warning_level=3', 'werror=true'])

lua = dependency('luajit')
deps = [lua]
deps2 = [lua]
name_suffix = []

if host_machine.system() == 'windows'
  # neoclip.w32
  target = 'w32'
  sources = ['neoclip_w32.c', 'neo_common.c']
  c_args = '-DPLATFORM=WinAPI'
elif host_machine.system() == 'darwin'
  appkit = dependency('AppKit', method : 'extraframework')
  add_languages('objc')
  # neoclip.mac
  target = 'mac'
  sources = ['neoclip_mac.m', 'neo_common.c']
  c_args = '-DPLATFORM=AppKit'
  deps += appkit
  name_suffix = 'so'
else # *nix
  x11 = dependency('X11')
  threads = dependency('threads')
  # neoclip.x11
  target = 'x11'
  sources = ['neoclip_nix.c', 'neo_x11.c', 'neo_common.c']
  c_args = '-DPLATFORM=X11'
  deps += [x11, threads]
  # try finding Wayland
  wl_client = dependency('wayland-client', required : false)
  wl_scanner = find_program('wayland-scanner', required : false, native : true)
  if wl_client.found() and wl_scanner.found()
    wlr_data_control_xml = 'extra/wlr-data-control-unstable-v1.xml'
    wlr_data_control_h = custom_target('wlr-data-control-h',
      input : wlr_data_control_xml,
      output : 'wayland-wlr-data-control-client-protocol.h',
      command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
    wlr_data_control_c = custom_target('wlr-data-control-c',
      input : wlr_data_control_xml, output : 'wayland-wlr-data-control-protocol.c',
      command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
    # neoclip.wl
    target2 = 'wl'
    sources2 = ['neoclip_nix.c', 'neo_wayland.c', 'neo_common.c', wlr_data_control_h,
      wlr_data_control_c]
    c_args2 = '-DPLATFORM=Wayland'
    deps2 += [wl_client, threads]
  endif
endif

shared_module(target, sources, c_args : c_args, dependencies : deps,
  gnu_symbol_visibility : 'internal', name_prefix : '', name_suffix : name_suffix,
  install : true, install_dir : 'lua/neoclip')

if is_variable('target2')
  shared_module(target2, sources2, c_args : c_args2, dependencies : deps2,
    gnu_symbol_visibility : 'internal', name_prefix : '', name_suffix : name_suffix,
    install : true, install_dir : 'lua/neoclip')
endif
