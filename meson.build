project(
    'matrix-tui',
    'c',
    default_options: [
        'buildtype=debugoptimized',
        'warning_level=3',
        'b_lto=true',
        'c_std=c11',
    ],
)

cc = meson.get_compiler('c')

src = [
    'src/cache.c',
    'src/draw.c',
    'src/header_libs.c',
    'src/login_form.c',
    'src/main.c',
    'src/message_buffer.c',
    'src/queue.c',
    'src/queue_callbacks.c',
    'src/render_message.c',
    'src/room_ds.c',
    'third_party/termbox-widgets/input.c',
    'third_party/termbox-widgets/tree.c',
    'third_party/termbox-widgets/ui_common.c',
]

c_args = [
    '-D_GNU_SOURCE',
    '-D_FORTIFY_SOURCE=2',
    '-DLOG_PATH="/tmp/matrix-tui.log"',
    '-DBUG_URL="https://github.com/git-bruh/matrix-tui/issues"',
    '-fstack-protector-strong',
    '--param=ssp-buffer-size=4',
    ['--include', 'src/fatal.h'],
]

warning_c_args = [
    '-Wshadow',
    '-Wnull-dereference',
    '-Wformat=2',
    '-Wcast-qual',
    '-Wconversion',
    '-Wpointer-arith',
    '-Wunused-macros',
    '-Wredundant-decls',
    '-Wwrite-strings',
    '-Werror=int-conversion',
    '-Werror=implicit-function-declaration',
    '-Werror=incompatible-pointer-types',
]

add_project_arguments(c_args, language: 'c')
add_project_arguments(cc.get_supported_arguments(warning_c_args), language: 'c')

libmatrix_proj = subproject('libmatrix')
libmatrix_dep = libmatrix_proj.get_variable('libmatrix_dep')

threads_dep = dependency('threads', required: true)
lmdb_dep = cc.find_library('lmdb', required: true)
m_dep = cc.find_library('m', required: false)

incdirs = [
    include_directories('third_party/stb', is_system: true),
    include_directories('third_party/termbox2', is_system: true),
    include_directories('third_party/termbox-widgets'),
]

executable(
    'matrix-tui',
    files(src),
    dependencies: [libmatrix_dep, lmdb_dep, threads_dep, m_dep],
    include_directories: incdirs,
    install: true,
)
