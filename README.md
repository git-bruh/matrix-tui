# matrix-tui

A terminal based Matrix client, WIP.

# TODO

- [ ] UI
  - [x] Login
  - [ ] Register
  - [ ] Autocomplete Usernames
  - [ ] Typing Indicators
  - [ ] Indicators For Unread Messages
  - [ ] Treeview
    - [x] Navigation (Including Nested Spaces)
    - [ ] Calculate Orphaned Rooms For Root
    - [ ] Bottom Status Bar For Traversing Space Path
    - [ ] Group Rooms
      - [ ] DMs
      - [ ] Invites
      - [x] Spaces
  - [ ] Maintaining State Of Rooms
    - [ ] Previously Orphaned Room Added To A Space
    - [ ] Room Removed From Space And Orphaned
    - [ ] Room Removed From Space And Present In Another Space
    - [ ] DM converted to Room / Room converted to DM
    - [ ] Invite Accepted
    - [x] Room Joined
    - [ ] Room Left / Space Left, Orphaning All Rooms Under It
    - [ ] Room Topic Changed
    - [ ] Room Name Changed
  - [ ] Fuzzy Search For Rooms
  - [ ] Message Buffer
    - [x] Word Wrap
    - [ ] HTML Rendering
      - [ ] Markdown
      - [ ] Syntax Highlighting Code
    - [ ] Interactive / Clickable Elements
      - [ ] Horizontally Scrollable Code Blocks
      - [ ] Clickable URLs / Hyperlinks
- [ ] Rooms
  - [ ] Sending Messages
    - [x] Plaintext
    - [ ] Markdown
    - [ ] Mentioning Users
    - [ ] Replies
  - [ ] Editing Messages
  - [ ] Display Topic
  - [ ] Joining (Invites or by ID)
  - [ ] Pagination
  - [ ] Reactions
- [ ] Write Tests


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
