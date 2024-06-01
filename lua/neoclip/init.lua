--[[
    neoclip - Neovim clipboard provider
    Last Change:    2024 May 31
    License:        https://unlicense.org
    URL:            https://github.com/matveyt/neoclip
--]]

local function has(...)
    return vim.call("has", ...) == 1
end

local function prequire(...)
    local ok, module = pcall(require, ...)
    return ok and module.start(true) and module
end

if has"win32" then
    neoclip = prequire"neoclip.w32"
elseif has"mac" then
    neoclip = prequire"neoclip.mac"
elseif has"unix" then
    neoclip = os.getenv"WAYLAND_DISPLAY" and prequire"neoclip.wl"
        or prequire"neoclip.x11"
else
    neoclip = false
end

if neoclip then
    -- suspend/resume events
    if vim.api.nvim_create_augroup then
        local group = vim.api.nvim_create_augroup("neoclip", {})
        vim.api.nvim_create_autocmd("VimSuspend", { group=group, callback=neoclip.stop })
        vim.api.nvim_create_autocmd("VimResume", { group=group, callback=neoclip.start })
    else
        vim.cmd[[
            augroup neoclip | au!
                autocmd VimSuspend * lua neoclip.stop()
                autocmd VimResume * lua neoclip.start()
            augroup end
        ]]
    end

    -- register provider
    vim.g.clipboard = {
        name = neoclip.id(),
        copy = {
            ["+"] = function(...) return neoclip.set("+", ...) end,
            ["*"] = function(...) return neoclip.set("*", ...) end,
        },
        paste = {
            ["+"] = function() return neoclip.get"+" end,
            ["*"] = function() return neoclip.get"*" end,
        },
    }

    -- reload provider
    if vim.g.loaded_clipboard_provider then
        vim.g.loaded_clipboard_provider = nil
        vim.cmd"runtime autoload/provider/clipboard.vim"
    end
else
    vim.api.nvim_err_writeln"neoclip: failed to load binary module!"
end

return neoclip
