#
# neoclip - Neovim clipboard provider
# Last Change:  2024 Jul 01
# License:      https://unlicense.org
# URL:          https://github.com/matveyt/neoclip
#


project('neoclip', 'c', license : 'Unlicense', meson_version: '>=0.56.0',
  default_options : ['b_ndebug=if-release', 'buildtype=release', 'c_std=c99',
    'install_umask=0177', 'prefix=' + meson.project_source_root() / '..', 'strip=true',
    'warning_level=3', 'werror=true'])


lua = dependency('luajit')
deps1 = [lua]
deps2 = [lua]
name_suffix = []


if host_machine.system() == 'windows'
  # neoclip.w32
  target1 = 'w32'
  sources1 = ['neoclip_w32.c', 'neo_common.c']
  c_args1 = '-DPLATFORM=WinAPI'

elif host_machine.system() == 'darwin'
  appkit = dependency('AppKit', method : 'extraframework')
  add_languages('objc')
  # neoclip.mac
  target1 = 'mac'
  sources1 = ['neoclip_mac.m', 'neo_common.c']
  c_args1 = '-DPLATFORM=AppKit'
  deps1 += appkit
  name_suffix = 'so'

else # *nix
  x11 = dependency('X11')
  threads = dependency('threads')
  # neoclip.x11
  target1 = 'x11'
  sources1 = ['neoclip_nix.c', 'neo_x11.c', 'neo_common.c']
  c_args1 = '-DPLATFORM=X11'
  deps1 += [x11, threads]

  # try finding Wayland
  wl_client = dependency('wayland-client', required : false)
  wlr_data_control_xml = 'extra/wlr-data-control-unstable-v1.xml'

  # this needs meson.version() >= 0.62.0
  # for now we keep using custom_target() instead
  # wlr_data_control =
  #   import('unstable-wayland').scan_xml(wlr_data_control_xml).to_list()

  wl_scanner = find_program('wayland-scanner', required : false, native : true)
  if wl_scanner.found()
    wlr_data_control = [
      custom_target('wlr-data-control-c', input : wlr_data_control_xml,
        output : 'wayland-wlr-data-control-protocol.c',
        command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
      custom_target('wlr-data-control-h', input : wlr_data_control_xml,
        output : 'wayland-wlr-data-control-client-protocol.h',
        command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
    ]
  endif

  if wl_client.found() and is_variable('wlr_data_control')
    # neoclip.wl
    target2 = 'wl'
    sources2 = ['neoclip_nix.c', 'neo_wayland.c', 'neo_common.c'] + wlr_data_control
    c_args2 = '-DPLATFORM=Wayland'
    deps2 += [wl_client, threads]
  endif
endif


if is_variable('target1')
  shared_module(target1, sources1, c_args : c_args1, dependencies : deps1,
    gnu_symbol_visibility : 'internal', name_prefix : '', name_suffix : name_suffix,
    install : true, install_dir : 'lua/neoclip')
endif

if is_variable('target2')
  shared_module(target2, sources2, c_args : c_args2, dependencies : deps2,
    gnu_symbol_visibility : 'internal', name_prefix : '', name_suffix : name_suffix,
    install : true, install_dir : 'lua/neoclip')
endif
