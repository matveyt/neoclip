" Neovim clipboard provider
" Last Change:  2022 May 18
" License:      https://unlicense.org
" URL:          https://github.com/matveyt/neoclip

if exists('g:loaded_neoclip') || exists('g:clipboard') || !has('nvim')
    finish
endif

lua<<
    local function prequire(...)
        local ok, module = pcall(require, ...)
        return ok and module.start() and module
    end

    if vim.fn.has"win32" == 1 then
        neoclip = prequire"neoclip_w32"
    elseif vim.fn.has"mac" == 1 then
        neoclip = prequire"neoclip_mac"
    elseif vim.fn.has"unix" == 1 then
        neoclip = os.getenv"WAYLAND_DISPLAY" and prequire"neoclip_wl"
            or prequire"neoclip_x11"
    end

    vim.g.loaded_neoclip = not not neoclip
.

if g:loaded_neoclip
    augroup neoclip | au!
        autocmd VimSuspend * lua neoclip.stop()
        autocmd VimResume * lua neoclip.start()
    augroup end

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
