" Neovim clipboard provider
" Maintainer:   matveyt
" Last Change:  2021 Apr 09
" License:      https://unlicense.org
" URL:          https://github.com/matveyt/neoclip

if exists('g:loaded_neoclip') || !has('nvim')
    finish
endif

let s:save_cpo = &cpo
set cpo&vim

lua<<
    local prequire = function(name)
        local ok, module = pcall(require, name)
        return ok and module or nil
    end
    if vim.fn.has("win32") == 1 then
        neoclip = prequire("neoclip_w32")
    elseif vim.fn.has("mac") == 1 then
        neoclip = prequire("neoclip_mac")
    elseif vim.fn.has("unix") == 1 then
        neoclip = os.getenv("WAYLAND_DISPLAY") and
            prequire("neoclip_wl") or prequire("neoclip_x11")
    end
    vim.g.loaded_neoclip = neoclip and neoclip.start() or false
    if vim.g.loaded_neoclip then
        neoclip.partial = function(action, reg)
            return function(...)
                return neoclip[action](reg, ...)
            end
        end
    end
.

let g:clipboard = g:loaded_neoclip ? {
    \ 'name': luaeval('neoclip.id()'),
    \ 'paste': {
    \   '+': luaeval('neoclip.partial("get", "+")'),
    \   '*': luaeval('neoclip.partial("get", "*")'),
    \ },
    \ 'copy': {
    \   '+': luaeval('neoclip.partial("set", "+")'),
    \   '*': luaeval('neoclip.partial("set", "*")'),
    \ },
\ } : {
    \ 'name': 'neoclip/Void',
    \ 'paste': {
    \   '+': {-> []},
    \   '*': {-> []},
    \ },
    \ 'copy': {
    \   '+': {-> v:true},
    \   '*': {-> v:true},
    \ },
\ }

let &cpo = s:save_cpo
unlet s:save_cpo
