--[[
    neoclip - Neovim clipboard provider
    Last Change:    2025 Mar 17
    License:        https://unlicense.org
    URL:            https://github.com/matveyt/neoclip
--]]


-- load default driver
if not vim.g.loaded_clipboard_provider then
    require"neoclip".setup()
end
