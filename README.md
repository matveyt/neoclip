### Description

This is [Neovim](https://neovim.io) clipboard provider. It allows to access system
clipboard without any extra tools, such as `win32yank`, `xclip`, `xsel`, `pbcopy`,
`pbpaste` and so on.

Read `:h provider-clipboard` for more information on Neovim clipboard integration.

### Installation

First, fetch the plugin using any plugin manager you like, or simply clone it with `git`
under your packages directory tree, see `:h packages`.

An example assuming you have [minpac](https://github.com/k-takata/minpac):

1. Add to your `init.vim`
```
    call minpac#init()
    call minpac#add('matveyt/neoclip', #{type: 'opt'})
    "... more plugins to follow

    if has('nvim')
        packadd! neoclip
        lua require"neoclip":setup()
    endif
```

2. Save the file and reload configuration
```
    :update | source %
```

3.  Update the plugin from network repository
```
    :call minpac#update('neoclip')
```

Next, drop to your shell and compile platform-dependent module from source.

4. Compiling
```
    $ cd ~/.config/nvim/pack/minpac/start/neoclip/src

    $ # by CMake
    $ cmake -B build
    $ cmake --build build
    $ cmake --install build --strip

    $ # ..or by Meson
    $ meson setup build
    $ meson install -C build --strip
```

5. Run Neovim again and see if it's all right
```
    :checkhealth neoclip
```

### Compatibility and other troubles

Currently Neoclip should run on Windows, macOS and all the various \*nix'es (with X11
and/or Wayland display server). See `:h neoclip-build` to get more information on build
dependencies. See `:h neoclip-issues` for a list of known issues.
