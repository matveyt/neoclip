### Description

This is Neovim clipboard provider. It allows to access system clipboard without any
additional tools, such as `win32yank`, `xclip`, `xsel`, `pbcopy`, `pbpaste` etc.

See `:h provider-clipboard` for more info.

Supported platforms are Windows, macOS, X11 and Wayland. You have to compile
platform-dependent module from source before use. For example,

    $ cd ~/.config/nvim/pack/bundle/opt/neoclip/src
    $ cmake -B build -G Ninja && ninja -C build install/strip
