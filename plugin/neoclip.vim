" Neovim clipboard provider
" Maintainer:   matveyt
" Last Change:  2020 Aug 13
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

function s:get(regname) abort
    if g:neoclip_channel <= 0
        return luaeval('neoclip.get(_A)', a:regname)
    else
        return rpcrequest(g:neoclip_channel, 'Gui', 'GetClipboard', a:regname)
    endif
endfunction

function s:set(regname, lines, regtype) abort
    if g:neoclip_channel <= 0
        call luaeval('neoclip.set(_A[1], _A[2], _A[3])',
            \ [a:regname, a:lines, a:regtype])
    else
        call rpcnotify(g:neoclip_channel, 'Gui', 'SetClipboard',
            \ a:lines, a:regtype, a:regname)
    endif
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

function s:reset_module() abort
    lua<<
    if vim.g.neoclip_channel <= 0 then
        -- load C module
        if vim.fn.has("win32") == 1 then
            neoclip = require"neoclip_w32"
        elseif vim.fn.has("unix") == 1 then
            neoclip = require"neoclip_x11"
        else
            vim.api.nvim_command "throw 'neoclip: Unsupported platform'"
        end
        -- try to start it up
        if neoclip.start ~= nil and not neoclip.start() then
            vim.api.nvim_command "throw 'neoclip: Module failed to start'"
        end
    else
        -- stop C module
        if neoclip ~= nil and neoclip.stop ~= nil then
            neoclip.stop()
        end
    end
.
endfunction

call s:reset_module()
command! -count -bar Neoclip let g:neoclip_channel = <count> | call s:reset_module()

let &cpo = s:save_cpo
unlet s:save_cpo
