### Description

This is Neovim clipboard provider. It allows to access system clipboard without any
additional tools, such as `win32yank`, `xclip`, `xsel`, `pbcopy`, `pbpaste` etc.

See `:h provider-clipboard` for more info.

Currently supported platforms are Windows, macOS and X. You have to compile
platform-dependent module from source before use. For example,

    $ cd ~/.config/nvim/pack/bundle/opt/neoclip/src
    $ meson build && ninja -C build install
