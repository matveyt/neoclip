" Neovim clipboard provider
" Maintainer:   matveyt
" Last Change:  2021 Apr 14
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
    if vim.fn.has("win32") == 1 then
        neoclip = prequire("neoclip_w32")
    elseif vim.fn.has("mac") == 1 then
        neoclip = prequire("neoclip_mac")
    elseif vim.fn.has("unix") == 1 then
        neoclip = os.getenv("WAYLAND_DISPLAY") and
            prequire("neoclip_wl") or prequire("neoclip_x11")
    end
    vim.g.loaded_neoclip = neoclip and neoclip.start() or false
.

if g:loaded_neoclip
    let g:clipboard = #{name: luaeval('neoclip.id()'), copy: {}, paste: {}}
    let g:clipboard.copy['+'] = luaeval('function(...) return neoclip.set("+", ...) end')
    let g:clipboard.copy['*'] = luaeval('function(...) return neoclip.set("*", ...) end')
    let g:clipboard.paste['+'] = luaeval('function() return neoclip.get("+") end')
    let g:clipboard.paste['*'] = luaeval('function() return neoclip.get("*") end')
else
    let g:clipboard = #{name: 'neoclip/Void', copy: {}, paste: {}}
    let g:clipboard.copy['+'] = function({-> v:true})
    let g:clipboard.copy['*'] = g:clipboard.copy['+']
    let g:clipboard.paste['+'] = function({-> []})
    let g:clipboard.paste['*'] = g:clipboard.paste['+']
endif
