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

src_app = [
    'src/app/queue_callbacks.c',
    'src/app/queue_callbacks.h',
    'src/app/room_ds.c',
    'src/app/room_ds.h',
    'src/app/state.h',
]

src_db = [
    'src/db/cache.c',
    'src/db/cache.h',
]

src_header_libs = [
    'src/header_libs/stb_ds.c',
    'src/header_libs/termbox.c',
    'src/header_libs/widgets.c',
]

src_ui = [
    'src/ui/draw.c',
    'src/ui/login_form.c',
    'src/ui/login_form.h',
    'src/ui/message_buffer.c',
    'src/ui/message_buffer.h',
    'src/ui/render_message.c',
    'src/ui/ui.h',
]

src_util = [
    'src/util/fatal.h',
    'src/util/log.h',
    'src/util/queue.c',
    'src/util/queue.h',
    'src/util/scoped_globals.c',
]

src_main = src_app + src_db + src_header_libs + src_ui + src_util + 'src/main.c'

tests = [
    'test_cache',
    'test_login_form',
    'test_message_buffer',
    'test_queue',
    'test_render_message',
    'test_room_ds',
]

tests = []

c_args = [
    '-D_GNU_SOURCE',
    '-D_FORTIFY_SOURCE=2',
    '-DBUG_URL="https://github.com/git-bruh/matrix-tui/issues"',
    '-fstack-protector-strong',
    '--param=ssp-buffer-size=4',
    ['--include', 'src/util/fatal.h'],
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

if get_option('b_sanitize').contains('undefined') and cc.get_id() == 'clang'
    add_global_arguments('-fsanitize-blacklist=../asan.supp', language: 'c')
endif

libmatrix_proj = subproject('libmatrix')
libmatrix_dep = libmatrix_proj.get_variable('libmatrix_dep')

threads_dep = dependency('threads', required: true)
lmdb_dep = cc.find_library('lmdb', required: false)
m_dep = cc.find_library('m', required: false)

if not lmdb_dep.found()
    lmdb_proj = subproject('lmdb', default_options: ['default_library=static'])
    lmdb_dep = lmdb_proj.get_variable('lmdb_dep')
endif

deps = [libmatrix_dep, lmdb_dep, threads_dep, m_dep]

incdirs = [
    include_directories('src'),
    include_directories('src/assert_override_hack'),
    include_directories('third_party/stb', is_system: true),
    include_directories('third_party/termbox2', is_system: true),
    include_directories('third_party/termbox-widgets'),
]

if get_option('tests')
    unity_proj = subproject('unity', default_options: ['werror=false'])
    unity_dep = unity_proj.get_variable('unity_dep')

    foreach test_name : tests
        exe = executable(
            test_name,
            common_src + 'tests/stub.c' + 'tests/@0@.c'.format(test_name),
            dependencies: deps + unity_dep,
            include_directories: incdirs + include_directories('src'),
        )
        test(test_name, exe)
    endforeach
endif

executable(
    'matrix-tui',
    src_main,
    dependencies: deps,
    include_directories: incdirs,
    install: true,
)
