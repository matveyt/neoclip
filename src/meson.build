#
# neoclip - Neovim clipboard provider
# Last Change:  2024 Jul 14
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
  # w32-driver
  target1 = 'w32-driver'
  sources1 = ['neoclip_w32.c', 'neo_common.c']

elif host_machine.system() == 'darwin'
  appkit = dependency('AppKit', method : 'extraframework')
  add_languages('objc')
  # mac-driver
  target1 = 'mac-driver'
  sources1 = ['neoclip_mac.m', 'neo_common.c']
  deps1 += appkit
  name_suffix = 'so'

else # *nix
  x11 = dependency('X11')
  threads = dependency('threads')
  # x11-driver
  target1 = 'x11-driver'
  sources1 = ['neoclip_nix.c', 'neo_x11.c', 'neo_common.c']
  deps1 += [x11, threads]

  # try finding Wayland
  wl_client = dependency('wayland-client', required : false)
  wlr_data_control_xml = 'extra/wlr-data-control-unstable-v1.xml'

  # 'unstable-wayland' requires meson.version() >= 0.62.0
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
    # wl-driver
    target2 = 'wl-driver'
    sources2 = ['neoclip_nix.c', 'neo_wayland.c', 'neo_common.c'] + wlr_data_control
    deps2 += [wl_client, threads]
  endif
endif


if is_variable('target1')
  shared_module(target1, sources1, dependencies : deps1,
    gnu_symbol_visibility : 'internal', install : true, install_dir : 'lua/neoclip',
    name_prefix : '', name_suffix : name_suffix)
endif

if is_variable('target2')
  shared_module(target2, sources2, dependencies : deps2,
    gnu_symbol_visibility : 'internal', install : true, install_dir : 'lua/neoclip',
    name_prefix : '', name_suffix : name_suffix)
endif
