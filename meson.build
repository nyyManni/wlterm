project('wlterm', 'c', version: '0.1.0', license: 'MIT')

cc                  = meson.get_compiler('c')

freetype            = dependency('freetype2')
xkbcommon           = dependency('xkbcommon')
rt                  = cc.find_library('rt')
m                   = cc.find_library('m')
msdfgl              = cc.find_library('msdfgl')
wayland_egl         = dependency('wayland-egl')
gl                  = dependency('GL')
egl                 = cc.find_library('EGL')
wayland_client      = dependency('wayland-client')

wayland_protos      = dependency('wayland-protocols')

wayland_scanner_dep = dependency('wayland-scanner')
wayland_scanner = find_program(wayland_scanner_dep.get_pkgconfig_variable('wayland_scanner'))
wl_protocol_dir = wayland_protos.get_pkgconfig_variable('pkgdatadir')

protocols = [[wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml']]

protos_src = []
protos_headers = []

foreach p : protocols
  xml = join_paths(p)
  protos_src += custom_target(
    xml.underscorify() + '_client_c',
    input: xml,
    output: '@BASENAME@-protocol.c',
    command: [wayland_scanner, 'public-code', '@INPUT@', '@OUTPUT@'],
  )
  protos_headers += custom_target(
    xml.underscorify() + '_client_h',
    input: xml,
    output: '@BASENAME@-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
endforeach

# shaders = run_command('find' ,'src', '-name', '*.glsl').stdout().strip().split(' ')

# shader_header = custom_target(
#   output: '_wlterm_shaders.h'
# )

# message(shaders)
# foreach p : shaders
#   message(p)
# endforeach


wlterm_src = ['src/main.c', 'src/egl_util.c', 'src/wlterm.c'] + protos_src + protos_headers
wlterm_deps = [wayland_client, msdfgl, rt, m, freetype, egl, gl, wayland_egl, xkbcommon]

executable('wlterm', wlterm_src, dependencies: wlterm_deps, install: true)
