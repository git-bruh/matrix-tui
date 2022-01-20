project('matrix-tui', 'c')

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
	'third_party/termbox-widgets/ui_common.c'
]

c_args = [
	'-O3',
	'-std=c11',
	'-D_GNU_SOURCE',
	'-D_FORTIFY_SOURCE=2',
	'-fstack-protector-strong',
	'--param=ssp-buffer-size=4',
	'-Wall',
	'-Wextra',
	'-Wpedantic',
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
	'-Werror=incompatible-pointer-types'
]

cc = meson.get_compiler('c')

libmatrix_proj = subproject('libmatrix')
libmatrix_dep = libmatrix_proj.get_variable('libmatrix_dep')

threads_dep = dependency('threads', required: true)
lmdb_dep = cc.find_library('lmdb', required: true)
m_dep = cc.find_library('m', required: false)

incdirs = [
	include_directories('src'),
	include_directories('third_party/stb'),
	include_directories('third_party/termbox2'),
	include_directories('third_party/termbox-widgets'),
]

log_path = '-DLOG_PATH="/tmp/matrix-tui.log"'
bug_url = '-DBUG_URL="https://github.com/git-bruh/matrix-tui/issues"'

add_project_arguments('--include', 'src/fatal.h', language: 'c')
add_project_arguments('-D_GNU_SOURCE', language: 'c')
add_project_arguments(log_path, language: 'c')
add_project_arguments(bug_url, language: 'c')

executable(
	'matrix-tui',
	files(src),
	dependencies: [libmatrix_dep, lmdb_dep, threads_dep, m_dep],
	include_directories: incdirs,
	install: true
)
