" Neovim clipboard provider
" Maintainer:   matveyt
" Last Change:  2020 Aug 25
" License:      https://unlicense.org
" URL:          https://github.com/matveyt/neoclip

if exists('g:loaded_neoclip') || !has('nvim')
    finish
endif

let s:save_cpo = &cpo
set cpo&vim

" load Lua module
lua<<
    if vim.fn.has("win32") == 1 then
        neoclip = require("neoclip_w32")
    elseif vim.fn.has("mac") == 1 then
        neoclip = require("neoclip_mac")
    elseif vim.fn.has("unix") == 1 then
        neoclip = require("neoclip_x11")
    end
    vim.g.loaded_neoclip = neoclip and (not neoclip.start or neoclip.start())
.

if g:loaded_neoclip
    function s:get(regname) abort
        return luaeval('neoclip.get(_A)', a:regname)
    endfunction

    function s:set(regname, lines, regtype) abort
        call luaeval('neoclip.set(_A[1], _A[2], _A[3])',
            \ [a:regname, a:lines, a:regtype])
    endfunction

    let g:clipboard = {
        \   'name': 'neoclip',
        \   'paste': {
        \       '+': function('s:get', ['+']),
        \       '*': function('s:get', ['*']),
        \   },
        \   'copy': {
        \       '+': function('s:set', ['+']),
        \       '*': function('s:set', ['*']),
        \   },
    \ }
else
    echomsg 'neoclip: Unsupported platform'
endif

let &cpo = s:save_cpo
unlet s:save_cpo
