" Neovim clipboard provider
" Maintainer:   matveyt
" Last Change:  2020 Aug 06
" License:      https://unlicense.org
" URL:          https://github.com/matveyt/neoclip

if exists('g:loaded_neoclip') || !has('nvim')
    finish
endif
let g:loaded_neoclip = 1

let s:save_cpo = &cpo
set cpo&vim

if !exists('g:neoclip_channel')
    let g:neoclip_channel = nvim_list_uis()[-1].chan
endif

if g:neoclip_channel <= 0
    if has('win32')
        lua require("neoclip_w32")
    elseif has('unix')
        lua require("neoclip_x11")
    else
        echoerr 'neoclip: Unsupported platform'
        finish
    endif
endif

function s:get(regname) abort
    if g:neoclip_channel > 0
        return rpcrequest(g:neoclip_channel, 'Gui', 'GetClipboard', a:regname)
    else
        return luaeval('neoclip.get(_A)', a:regname)
    endif
endfunction

function s:set(regname, lines, regtype) abort
    if g:neoclip_channel > 0
        call rpcnotify(g:neoclip_channel, 'Gui', 'SetClipboard',
            \ a:lines, a:regtype, a:regname)
    else
        call luaeval('neoclip.set(_A[1], _A[2], _A[3])',
            \ [a:regname, a:lines, a:regtype])
    endif
endfunction

let g:clipboard = {
    \   'name': 'neoclip',
    \   'copy': {
    \       '+': function('s:set', ['+']),
    \       '*': function('s:set', ['*']),
    \   },
    \   'paste': {
    \       '+': function('s:get', ['+']),
    \       '*': function('s:get', ['*']),
    \   },
\ }

let &cpo = s:save_cpo
unlet s:save_cpo
