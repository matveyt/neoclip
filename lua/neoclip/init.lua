--[[
    neoclip - Neovim clipboard provider
    Last Change:    2024 Jun 25
    License:        https://unlicense.org
    URL:            https://github.com/matveyt/neoclip
--]]


neoclip = {
    -- driver = require"neoclip.XYZ"
    -- issues = {"array", "of", "strings"}
    --
    -- issue(format, args)
    -- require(driver_name)
    -- register(true_by_default)
    -- setup(driver_name_or_nil)
}

function neoclip:issue(...)
    self.issues[#self.issues + 1] = string.format(...)
end

function neoclip:require(driver_name)
    local status, result1, result2

    status, result1 = pcall(require, driver_name)
    if status then
        status, result2 = pcall(result1.start)
        if status then
            self.driver = result1
        else
            self:issue("'%s' failed to start", driver_name)
            self:issue("%s", result2)
        end
    else
        self:issue("'%s' failed to load", driver_name)
        self:issue("%s", result1)
    end

    return status
end

function neoclip:register(arg)
    if arg ~= false then
        -- catch driver load failure
        assert(self.driver, "neoclip driver error (:checkhealth for info)")
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
        -- create autocmds
        if vim.api.nvim_create_augroup then
            local group = vim.api.nvim_create_augroup("neoclip", { clear = true })
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
        -- default clipboard
        vim.g.clipboard = nil
        vim.cmd[[
            if exists("#neoclip")
                autocmd! neoclip
                augroup! neoclip
            endif
        ]]
    end

    -- reload provider
    if vim.g.loaded_clipboard_provider then
        vim.g.loaded_clipboard_provider = nil
        vim.cmd"runtime autoload/provider/clipboard.vim"
    end
end

function neoclip:setup(driver_name)
    -- local helper
    local has = function(feat) return vim.fn.has(feat) == 1 end

    -- (re-)init self
    self.driver = nil
    self.issues = {}

    -- load driver
    if driver_name then
        self:require(driver_name)
    elseif has"win32" then
        self:require"neoclip.w32"
    elseif has"mac" then
        self:require"neoclip.mac"
    elseif has"unix" then
        -- Wayland first, fallback to X11
        local _ = vim.env.WAYLAND_DISPLAY and self:require"neoclip.wl"
            or self:require"neoclip.x11"
    end

    -- doesn't work with WSL
    if has"wsl" then
        self:issue"|neoclip-wsl| support is currently broken"
    end
    -- warn if clipboard=unnamed[plus]
    if vim.go.clipboard ~= "" then
        self:issue("'clipboard' option is set to *%s*", vim.go.clipboard)
    end

    self:register()
end


return neoclip
