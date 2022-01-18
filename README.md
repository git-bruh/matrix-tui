# matrix-tui

[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/git-bruh/matrix-tui.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/git-bruh/matrix-tui/context:cpp)

A terminal based Matrix client. WIP, not usable yet. Discard the following information for now.

# Pre-requisites

* C11 Compiler

* POSIX Make - `bmake` or `gmake`

* [cJSON](https://github.com/DaveGamble/cJSON)

* [cURL](https://github.com/curl/curl)

* [lmdb](https://github.com/LMDB/lmdb)

The above can be installed through your distribution's package manager. The corresponding package names are usually suffixed with `-dev`, such as `libcurl-dev` depending on your distribution.

A few dependencies are bundled:

* [libmatrix](https://github.com/git-bruh/libmatrix) - Library for interacting with matrix APIs.

* [stb](https://github.com/nothings/stb) - Data structures from `stb_ds.h`

* [termbox-widgets](https://github.com/git-bruh/termbox-widgets) - Widgets for termbox such as input fields and trees.

* [termbox2](https://github.com/termbox/termbox2) - The terminal rendering library


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

* Pass `--enable-sanitize` to `./configure` to enable sanitizers which help in finding memory leaks or undefined behaviour.

* Avoid triggering extra compiler warnings unless necessary.

* Run `make tidy` to run `clang-tidy` to lint the code.

* Before submitting a PR, format the code with `make format` which runs `clang-format`.
