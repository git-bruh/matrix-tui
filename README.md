# matrix-tui

A terminal based Matrix client. WIP, not usable yet. Discard the following information for now.

# Pre-requisites

* C11 Compiler

* Meson

* [cJSON](https://github.com/DaveGamble/cJSON)

* [cURL](https://github.com/curl/curl)

* [lmdb](https://github.com/LMDB/lmdb)

The above can be installed through your distribution's package manager. The corresponding package names are usually suffixed with `-dev`, such as `libcurl-dev` depending on your distribution.

A few dependencies are bundled:

* [libmatrix](https://github.com/git-bruh/libmatrix) - Library for interacting with matrix APIs.

* [stb](https://github.com/nothings/stb) - Data structures from `stb_ds.h`

* [termbox-widgets](https://github.com/git-bruh/termbox-widgets) - Widgets for termbox such as input fields and trees.

* [termbox2](https://github.com/termbox/termbox2) - The terminal rendering library


Make sure to run `git submodule update --remote --init --depth=1 -f` to clone them.

# Building

* Run `meson . build` and `ninja -C build` to build the project. The binary will be stored at `build/matrix-tui`

# Contributing

Contributions are always welcome, the following points should be kept in mind:

* Pass `-Db_sanitize=address,undefined` to `meson` to enable sanitizers which help in finding memory leaks or undefined behaviour.

* Before submitting a PR, format the code with `ninja -C build clang-format` which runs `clang-format`.
