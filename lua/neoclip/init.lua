--[[
    neoclip - Neovim clipboard provider
    Last Change:    2025 Jun 19
    License:        https://unlicense.org
    URL:            https://github.com/matveyt/neoclip
--]]


local neoclip = {
    -- driver = require"neoclip.XYZ"
    -- issues = {"array", "of", "strings"}
    --
    -- issue(fmt, ...)
    -- require(driver)
    -- register([clipboard])
    -- setup([driver])
}


function neoclip.issue(fmt, ...)
    neoclip.issues = neoclip.issues or {nil}  -- pre-allocate slot
    neoclip.issues[#neoclip.issues + 1] = fmt:format(...)
end

function neoclip.require(driver)
    local status, result1, result2

    status, result1 = pcall(require, driver)
    if status then
        status, result2 = pcall(result1.start)
        if status then
            neoclip.driver = result1
        else
            neoclip.issue("'%s' failed to start", driver)
            neoclip.issue("%s", result2)
            _G.package.loaded[driver] = nil
        end
    else
        neoclip.issue("'%s' failed to load", driver)
        neoclip.issue("%s", result1)
    end

    return status
end

function neoclip.register(clipboard)
    if clipboard == nil then
        -- catch driver load failure
        assert(neoclip.driver, "neoclip driver error (:checkhealth for info)")
        -- setup g:clipboard
        vim.g.clipboard = {
            name = neoclip.driver.id(),
            copy = {
                ["+"] = function(...) return neoclip.driver.set("+", ...) end,
                ["*"] = function(...) return neoclip.driver.set("*", ...) end,
            },
            paste = {
                ["+"] = function() return neoclip.driver.get"+" end,
                ["*"] = function() return neoclip.driver.get"*" end,
            },
            cache_enabled = false,
        }
        -- create autocmds
        if vim.api.nvim_create_augroup then
            local group = vim.api.nvim_create_augroup("neoclip", { clear=true })
            vim.api.nvim_create_autocmd("VimSuspend", { group=group,
                callback=neoclip.driver.stop })
            vim.api.nvim_create_autocmd("VimResume", { group=group,
                callback=neoclip.driver.start })
        else
            vim.cmd[[
                augroup neoclip | au!
                    autocmd VimSuspend * lua require"neoclip".driver.stop()
                    autocmd VimResume  * lua require"neoclip".driver.start()
                augroup end
            ]]
        end
    else
        vim.g.clipboard = clipboard
        vim.cmd[[
            if exists("#neoclip")
                autocmd! neoclip
                augroup! neoclip
            endif
        ]]
    end

    -- :h provider-reload
    vim.g.loaded_clipboard_provider = nil
    vim.cmd"runtime autoload/provider/clipboard.vim"
end

function neoclip.setup(arg1, arg2)
    -- local helper function
    local has = function(feat) return vim.fn.has(feat) == 1 end

    -- compat: accept both neoclip:setup() and neoclip.setup()
    local driver = (arg1 ~= neoclip) and arg1 or arg2

    -- (re-)init self
    if neoclip.driver then
        neoclip.driver.stop()
        neoclip.driver = nil
    end
    neoclip.issues = nil

    -- load driver
    if driver then
        neoclip.require(driver)
    elseif has"win32" then
        neoclip.require"neoclip.w32-driver"
    elseif has"mac" then
        neoclip.require"neoclip.mac-driver"
    elseif has"unix" then
        -- Wayland first, fallback to X11
        local _ = vim.env.WAYLAND_DISPLAY
            and (neoclip.require"neoclip.wl-driver"
                or neoclip.require"neoclip.wluv-driver")
            or (neoclip.require"neoclip.x11uv-driver"
                or neoclip.require"neoclip.x11-driver")
    else
        neoclip.issue"Unsupported platform"
    end

    -- warn if &clipboard is unnamed[plus]
    if vim.go.clipboard ~= "" then
        neoclip.issue("'clipboard' option is set to *%s*", vim.go.clipboard)
    end

    neoclip.register()
end


return neoclip
