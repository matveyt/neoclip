--[[
    neoclip - Neovim clipboard provider
    Last Change:    2024 Aug 23
    License:        https://unlicense.org
    URL:            https://github.com/matveyt/neoclip
--]]


neoclip = {
    -- driver = require"neoclip.XYZ"
    -- issues = {"array", "of", "strings"}
    --
    -- issue(fmt, ...)
    -- require(driver)
    -- register([clipboard])
    -- setup([driver])
}

function neoclip:issue(fmt, ...)
    self.issues = self.issues or {nil}  -- pre-allocate slot
    self.issues[#self.issues + 1] = fmt:format(...)
end

function neoclip:require(driver)
    local status, result1, result2

    status, result1 = pcall(require, driver)
    if status then
        status, result2 = pcall(result1.start)
        if status then
            self.driver = result1
        else
            self:issue("'%s' failed to start", driver)
            self:issue("%s", result2)
            _G.package.loaded[driver] = nil
        end
    else
        self:issue("'%s' failed to load", driver)
        self:issue("%s", result1)
    end

    return status
end

function neoclip:register(clipboard)
    if clipboard == nil then
        -- catch driver load failure
        assert(self.driver, "neoclip driver error (:checkhealth for info)")
        -- setup g:clipboard
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
            cache_enabled = false,
        }
        -- create autocmds
        if vim.api.nvim_create_augroup then
            local group = vim.api.nvim_create_augroup("neoclip", { clear=true })
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
        assert(type(clipboard) == "table", "table or nil expected")
        vim.g.clipboard = clipboard.copy and clipboard.paste and clipboard
            or clipboard[1] -- neoclip:register{false}
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

function neoclip:setup(driver)
    -- local helper
    local has = function(feat) return vim.fn.has(feat) == 1 end

    -- (re-)init self
    if self.driver then
        self.driver.stop()
        self.driver = nil
    end
    self.issues = {}

    -- load driver
    if driver then
        self:require(driver)
    elseif has"win32" then
        self:require"neoclip.w32-driver"
    elseif has"mac" then
        self:require"neoclip.mac-driver"
    elseif has"unix" then
        -- Wayland first, fallback to X11
        local _ = vim.env.WAYLAND_DISPLAY
            and (self:require"neoclip.wl-driver" or self:require"neoclip.wluv-driver")
            or (self:require"neoclip.x11-driver" or self:require"neoclip.x11uv-driver")
    else
        self:issue"Unsupported platform"
    end

    -- warn if clipboard=unnamed[plus]
    if vim.go.clipboard ~= "" then
        self:issue("'clipboard' option is set to *%s*", vim.go.clipboard)
    end

    self:register()
end


return neoclip
