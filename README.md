### Description

This is [Neovim](https://neovim.io) clipboard provider. It allows to access system
clipboard without any extra tools, such as `win32yank`, `xclip`, `xsel`, `pbcopy`,
`pbpaste` and so on.

Read `:h provider-clipboard` for more information on Neovim clipboard integration.

### Installation

First, fetch the plugin using any plugin manager you like, or simply clone it with `git`
under your packages directory tree, see `:h packages`.

An example assuming you use [minpac](https://github.com/k-takata/minpac):

1. Add to your `init.vim`
```
    call minpac#init()
    call minpac#add('matveyt/neoclip')
    "... more plugins to follow
```

2. Save the file and reload configuration
```
    :update | source %
```

3.  Update the plugin from network repository
```
    :call minpac#update('neoclip')
```

Next, drop to your shell and compile platform-dependent module from source. It can be
built with either CMake or Meson. The next example assumes you have CMake and Ninja as a
backend.

4. Compiling with CMake and Ninja
```
    $ cd ~/.config/nvim/pack/minpac/start/neoclip/src
    $ cmake -B build -G Ninja && ninja -C build install/strip
```

5. Run Neovim again and see if it's all right
```
    :checkhealth provider
```

### Compatibility and other troubles

Currently Neoclip should run on Windows, macOS and all the various \*nix'es (with X11
and/or Wayland display server up). See `:h neoclip-build` to get more information on
build dependencies. See `:h neoclip-issues` for a list of known issues.
