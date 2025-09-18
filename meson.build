project('quartz', 'c',
    version: '0.1',
    default_options : [
        'c_std=c23',
        # 'cpp_std=c23',
        'warning_level=2',
    ]
)

# Dependencies

wlroots_proj = subproject('wlroots',
    default_options : [
        'examples=false',
        'xwayland=enabled',
        'backends=auto,drm,libinput,x11',
        'renderers=gles2,vulkan',
    ]
)
wlroots_dep = wlroots_proj.get_variable('wlroots')

# wlroots_dep = dependency('wlroots-0.19')

wayland_server_dep = dependency('wayland-server')
xkbcommon_dep = dependency('xkbcommon')

deps = [
    wlroots_dep,
    wayland_server_dep,
    xkbcommon_dep,

    dependency('libinput'),
    dependency('xcb'),
    dependency('xcb-icccm')
]

# Quartz

args = [
    '-DWLR_USE_UNSTABLE',
    '-D_POSIX_C_SOURCE=200809L',
    '-DVERSION="0.1"',
    '-Wno-missing-field-initializers',
]

executable('quartz',
    'src/quartz.c',
    dependencies : deps,
    install : true,
    c_args : args,
    cpp_args : args,
    include_directories : [
        'src',
        'protocols',
    ]
)
