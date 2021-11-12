# matrix-tui

[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/git-bruh/matrix-tui.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/git-bruh/matrix-tui/context:cpp)

A terminal based Matrix client. WIP, not usable yet. Discard the following information for now.

# TODO

## Client

- [ ] Database
- [ ] UI

## Library

- [x] Syncing (Needs testing and some refactoring)
- [ ] Basic endpoints (Send / Delete / Edit)

# Pre-requisites

* C11 Compiler - `cproc`, `clang`, `gcc`

* POSIX Make - `bmake` or `gmake`

* [cJSON](https://github.com/DaveGamble/cJSON)

* [cURL](https://github.com/curl/curl)

The above can be installed through your distribution's package manager. The corresponding package names are usually suffixed with `-dev`, such as `libcurl-dev` depending on your distribution.

A few dependencies are bundled:

* [log.c](https://github.com/rxi/log.c) - A tiny ~200 LOC logging library

* [stb](https://github.com/nothings/stb) - Data structures from `stb_ds.h`

* [termbox](https://github.com/termbox/termbox2) - The terminal rendering library

Make sure to run `git submodule update --remote --init -f` to clone them.

# Building

* Run `./configure` to generate `config.mk`. The script takes the same arguments as an autohell `configure` script.

* Run `make` and then `make install` to install the binary. The `Makefile` also respectes the `$DESTDIR` variable to change the install location.

The following should suffice for regular builds:

```
./configure \
	--prefix=/usr \
	# --enable-static (Uncomment to build a fully static binary)

make
make install
```

# Contributing

Contributions are always welcome, the following points should be kept in mind:

* Pass `--enable-debug` to `./configure` to enable sanitizers which help in finding memory leaks or undefined behaviour.

* Avoid triggering extra compiler warnings unless necessary.

* Run `make tidy` to run `clang-tidy` to lint the code.

* Before submitting a PR, format the code with `make format` which runs `clang-format`.
