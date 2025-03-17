### Description

This is [Neovim][1] clipboard provider. It allows to access system clipboard without any
extra tools, such as `win32yank`, `xclip`, `xsel`, `pbcopy`, `pbpaste` and so on.

Read `:h provider-clipboard` for more information on Neovim clipboard integration.

### Installation

#### From source code

First, fetch the plugin using any plugin manager you like, or simply clone it with `git`
under your packages directory tree, see `:h packages`.

An example for [minpac][2]

1. Add to your `init.vim`
```
    call minpac#init()
    call minpac#add('matveyt/neoclip', #{type: 'opt'})
    "... more plugins to follow

    if has('nvim')
        packadd! neoclip
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

#### With Homebrew

There is a formula for `Homebrew` in [homebrew-neoclip][3] repository.

#### As a Nix Flake

Please, refer to [neoclip-flake][4] repository:
- an [example][5] which uses an overlay
- an [example][6] which uses a package directly

### Compatibility and other troubles

Neoclip should run on Windows, macOS and all the various \*nix'es (with X11 and/or
Wayland display server). See `:h neoclip-build` to get more information on build
dependencies. See `:h neoclip-issues` for a list of known issues.

[1]: https://neovim.io
[2]: https://github.com/k-takata/minpac
[3]: https://github.com/neoclip-nvim/homebrew-neoclip
[4]: https://github.com/neoclip-nvim/neoclip-flake
[5]: https://github.com/neoclip-nvim/neoclip-flake/blob/master/examples/with-overlay/flake.nix
[6]: https://github.com/neoclip-nvim/neoclip-flake/blob/master/examples/with-package/flake.nix
