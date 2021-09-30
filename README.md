### Description

This is Neovim clipboard provider. It allows to access system clipboard without any
additional tools, such as `win32yank`, `xclip`, `xsel`, `pbcopy`, `pbpaste` etc.

See `:h provider-clipboard` for more info.

Supported platforms are Windows, macOS, X11 and Wayland. You have to compile
platform-dependent module from source before use. For example,

    $ cd ~/.config/nvim/pack/bundle/opt/neoclip/src
    $ cmake -B build -G Ninja && ninja -C build install/strip


### Requirements

Yo will need some system packages to compile neoclip.
 - build-essential
 - cmake
 - ninja
 - luajit-devel


### Install

You can use a plugin manager to install, first make sure you have the requirements.

#### Example with Packer

[wbthomason/packer.nvim](https://github.com/wbthomason/packer.nvim)

```lua
-- plugins.lua
require("packer").startup(
    function()
        -- You should only need to include this 'use'
        use {
            'matveyt/neoclip',
            opt = true,
            run = 'cd src && cmake -B build -G Ninja && ninja -C build install/strip'
        }
    end
)
```
