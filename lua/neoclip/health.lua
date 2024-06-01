--[[
    neoclip - Neovim clipboard provider
    Last Change:    2024 May 30
    License:        https://unlicense.org
    URL:            https://github.com/matveyt/neoclip
--]]

local function neo_check()
    -- compatibility
    local h = vim.health or {
        start = require"health".report_start,
        info = require"health".report_info,
        ok = require"health".report_ok,
        warn = require"health".report_warn,
        error = require"health".report_error,
    }

    h.start"neoclip"

    if not neoclip then
        h.error("Not found", {
            "Do you |require()| it?",
            "Refer to |neoclip-build| on how to build from source code",
            "OS Windows, macOS and *nix (with X11 or Wayland) are supported"
        })
        return
    end

    if vim.g.clipboard ~= nil and vim.g.clipboard ~= vim.NIL and
        vim.g.clipboard.name == neoclip.id() then
        h.ok(neoclip.id() .. " is used")
    else
        h.error(neoclip.id() .. " is found but inactive", {
            "Do not install other clipboard provider",
            "See if there are any error messages on startup"
        })
    end
    if vim.o.clipboard ~= "" then
        h.info("NOTE 'clipboard' option is set to *" .. vim.o.clipboard .. "*")
    end

    local plus, star = vim.fn.getreginfo"+", vim.fn.getreginfo"*"
    neoclip.set("+", { "На дворе трава" }, "b")
    neoclip.set("*", { "На траве дрова" }, "b")
    local test_data = vim.fn.getreginfo"+"
    vim.fn.setreg("+", plus)
    vim.fn.setreg("*", star)

    if test_data.regtype ~= "\x1626" or #test_data.regcontents ~= 1 then
        h.error"Clipboard test failed"
    elseif test_data.regcontents[1] == "На дворе трава" then
        h.ok"Clipboard test passed"
    elseif test_data.regcontents[1] == "На траве дрова" then
        h.ok"Clipboard test passed"
        h.info"NOTE registers + and * are always equal"
    else
        h.error"Clipboard test failed"
    end
end

return { check = neo_check }
