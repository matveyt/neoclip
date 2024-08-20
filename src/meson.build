#
# neoclip - Neovim clipboard provider
# Last Change:  2024 Aug 20
# License:      https://unlicense.org
# URL:          https://github.com/matveyt/neoclip
#


project('neoclip', 'c', license : 'Unlicense', meson_version : '>=0.56.0',
  default_options : ['buildtype=release', 'c_std=c99', 'install_umask=0177',
    'prefix=' + meson.project_source_root() / '..', 'strip=true', 'warning_level=3',
    'werror=true'])


# selectively enable module build
w32_build   = true
mac_build   = true
x11_build   = true
x11uv_build = true
wl_build    = true
wluv_build  = true


# Lua(JIT) is always required
lua = dependency('luajit')


if host_machine.system() == 'windows'
  # w32-driver
  w32_target = 'w32-driver'
  w32_sources = ['neoclip_w32.c', 'neo_common.c']

elif host_machine.system() == 'darwin'
  add_languages('objc')
  appkit = dependency('AppKit', method : 'extraframework')

  # mac-driver
  mac_target = 'mac-driver'
  mac_sources = ['neoclip_mac.m', 'neo_common.c']
  mac_deps = [appkit]

else # *nix
  x11 = dependency('X11', required : false)
  threads = dependency('threads', required : false)
  wl_client = dependency('wayland-client', required : false)
  wl_scanner = find_program('wayland-scanner', required : false, native : true)
  if wl_scanner.found()
    wlr_data_control_xml = 'extra/wlr-data-control-unstable-v1.xml'
    wlr_data_control = [
      custom_target('wlr-data-control-c', input : wlr_data_control_xml,
        output : 'wayland-wlr-data-control-protocol.c',
        command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
      custom_target('wlr-data-control-h', input : wlr_data_control_xml,
        output : 'wayland-wlr-data-control-client-protocol.h',
        command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
    ]
  endif

  # x11-driver
  if x11.found() and threads.found()
    x11_target = 'x11-driver'
    x11_sources = ['neoclip_nix.c', 'neo_x11.c', 'neo_common.c']
    x11_deps = [x11, threads]
  endif

  # x11uv-driver
  if x11.found()
    x11uv_target = 'x11uv-driver'
    x11uv_sources = ['neoclip_nix.c', 'neo_x11_uv.c', 'neo_common.c']
    x11uv_deps = [x11]
  endif

  # wl-driver
  if wl_client.found() and is_variable('wlr_data_control') and threads.found()
    wl_target = 'wl-driver'
    wl_sources = ['neoclip_nix.c', 'neo_wayland.c', 'neo_common.c']
    wl_sources += wlr_data_control
    wl_deps = [wl_client, threads]
  endif

  # wluv-driver
  if wl_client.found() and is_variable('wlr_data_control')
    wluv_target = 'wluv-driver'
    wluv_sources = ['neoclip_nix.c', 'neo_wayland_uv.c', 'neo_common.c']
    wluv_sources += wlr_data_control
    wluv_deps = [wl_client]
  endif
endif

foreach t : ['w32', 'mac', 'x11', 'x11uv', 'wl', 'wluv']
  target = get_variable(t + '_target', '')
  sources = get_variable(t + '_sources', [])
  deps = get_variable(t + '_deps', []) + [lua]
  if target != '' and get_variable(t + '_build', false)
    message('Building `@0@\' target'.format(t))
    shared_module(target, sources, dependencies : deps,
      gnu_symbol_visibility : 'internal', install : true, install_dir : 'lua/neoclip',
      name_prefix : '', name_suffix : host_machine.system() == 'darwin' ? 'so' : [])
  endif
endforeach
