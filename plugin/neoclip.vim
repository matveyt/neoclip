" Neovim clipboard provider
" Maintainer:   matveyt
" Last Change:  2020 Jul 23
" License:      http://unlicense.org
" URL:          https://github.com/matveyt/neoclip

if exists('g:loaded_neoclip') || !has('nvim')
    finish
endif
let g:loaded_neoclip = 1

let s:save_cpo = &cpo
set cpo&vim

if has('win32')
    lua require("neoclip_w32")
else
    echoerr 'neoclip: Unsupported platform'
    finish
endif

function s:utf8(clip, ...) abort
    if &enc is# 'utf-8' || empty(a:clip)
        return a:clip
    else
        let [l:from, l:to] = get(a:, 1, v:true) ? [&enc, 'utf-8'] : ['utf-8', &enc]
        return [map(copy(a:clip[0]), {_, v -> iconv(v, l:from, l:to)}),
            \ iconv(a:clip[1], l:from, l:to)]
    endif
endfunction

let g:clipboard = {
    \   'name': 'neoclip',
    \   'copy': {
    \       '+': {lines, regtype -> v:lua.neoclip.setplus(s:utf8([lines, regtype]))},
    \       '*': {lines, regtype -> v:lua.neoclip.setstar(s:utf8([lines, regtype]))},
    \   },
    \   'paste': {
    \       '+': {-> s:utf8(v:lua.neoclip.getplus(), v:false)},
    \       '*': {-> s:utf8(v:lua.neoclip.getstar(), v:false)},
    \   },
\ }

let &cpo = s:save_cpo
unlet s:save_cpo
