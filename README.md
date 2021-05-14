### Description

This is Neovim clipboard provider. It allows to access OS clipboard without any external
tools, such as `win32yank`, `xclip`, `xsel`, `pbcopy`, `pbpaste` and so on.

See `:h provider-clipboard` for more info.

Currently supported platforms are Windows, macOS and X. You'll need Meson and LuaJIT to
compile platform-dependent module from source. For example,

    $ cd ~/.config/nvim/pack/bundle/opt/neoclip/src
    $ meson build && ninja -C build install
