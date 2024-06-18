--[[
    neoclip - Neovim clipboard provider
    Last Change:    2024 Jun 18
    License:        https://unlicense.org
    URL:            https://github.com/matveyt/neoclip
--]]


--[[ local helpers ]]

local function has(...)
    return vim.api.nvim_call_function("has", {...}) == 1
end

local function prequire(name, issues)
    local ok, value1, value2

    ok, value1 = pcall(require, name)
    if not ok then
        table.insert(issues, name .. " failed to load")
        table.insert(issues, value1)
        return
    end

    ok, value2 = pcall(value1.start)
    if not ok then
        table.insert(issues, name .. " failed to start")
        table.insert(issues, value2)
        return
    end

    return value1
end


--[[ module object ]]

neoclip = {
    driver = nil,
    issues = {}
}

function neoclip:register(action)
    if action ~= false then
        -- set g:clipboard
        vim.g.clipboard = {
            name = self.driver.id(),
            copy = {
                ["+"] = function(...) return self.driver.set("+", ...) end,
                ["*"] = function(...) return self.driver.set("*", ...) end,
            },
            paste = {
                ["+"] = function() return self.driver.get"+" end,
                ["*"] = function() return self.driver.get"*" end,
            },
        }
        -- create autocmd
        if vim.api.nvim_create_augroup then
            local group = vim.api.nvim_create_augroup("neoclip", {})
            vim.api.nvim_create_autocmd("VimSuspend", { group=group,
                callback=self.driver.stop })
            vim.api.nvim_create_autocmd("VimResume", { group=group,
                callback=self.driver.start })
        else
            vim.cmd[[
                augroup neoclip | au!
                    autocmd VimSuspend * lua neoclip.driver.stop()
                    autocmd VimResume  * lua neoclip.driver.start()
                augroup end
            ]]
        end
    else
        -- cancel registration
        vim.g.clipboard = nil
        vim.cmd[[
            if exists("#neoclip")
                autocmd! neoclip
                augroup! neoclip
            endif
        ]]
    end

    -- reload clipboard provider
    if vim.g.loaded_clipboard_provider then
        vim.g.loaded_clipboard_provider = nil
        vim.cmd"runtime autoload/provider/clipboard.vim"
    end
end


--[[ startup code ]]

-- load driver
if has"win32" then
    neoclip.driver = prequire("neoclip.w32", neoclip.issues)
elseif has"mac" then
    neoclip.driver = prequire("neoclip.mac", neoclip.issues)
elseif has"unix" then
    neoclip.driver = vim.env.WAYLAND_DISPLAY and prequire("neoclip.wl",
        neoclip.issues) or prequire("neoclip.x11", neoclip.issues)
end

-- extra issues
if has"wsl" then
    table.insert(neoclip.issues, "|neoclip-wsl| support is currently broken")
end

if vim.go.clipboard ~= "" then
    table.insert(neoclip.issues, string.format("'clipboard' option is set to *%s*",
        vim.go.clipboard))
end

-- register clipboard hooks
if neoclip.driver then
    neoclip:register()
else
    vim.api.nvim_err_writeln"neoclip: driver error (run :checkhealth for info)"
end

return neoclip
