" Neovim clipboard provider implementation
" Last Change:  2021 May 14
" License:      https://unlicense.org
" URL:          https://github.com/matveyt/neoclip

if exists('g:loaded_neoclip') || !has('nvim')
    finish
endif

lua<<
    local prequire = function(name)
        local ok, module = pcall(require, name)
        return ok and module or nil
    end
    if vim.fn.has"win32" ~= 0 then
        neoclip = prequire"neoclip_w32"
    elseif vim.fn.has"mac" ~= 0 then
        neoclip = prequire"neoclip_mac"
    elseif vim.fn.has"unix" ~= 0 then
        neoclip = os.getenv"WAYLAND_DISPLAY" and
            prequire"neoclip_wl" or prequire"neoclip_x11"
    end
    vim.g.loaded_neoclip = neoclip and neoclip.start() or false
.

if g:loaded_neoclip
    let g:clipboard = {
        \ 'name': luaeval('neoclip.id()'),
        \ 'copy': {
            \ '+': luaeval('function(...) return neoclip.set("+", ...) end'),
            \ '*': luaeval('function(...) return neoclip.set("*", ...) end')
        \ },
        \ 'paste': {
            \ '+': luaeval('function() return neoclip.get("+") end'),
            \ '*': luaeval('function() return neoclip.get("*") end')
        \ }
    \ }
endif
