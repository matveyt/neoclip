--[[
    neoclip - Neovim clipboard provider
    Last Change:    2024 Jun 26
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

    if not neoclip.driver then
        h.error("No driver module loaded", neoclip.issues or {
            "Have you run |neoclip:setup()|?"
        })
        return
    end

    if vim.g.clipboard and vim.g.clipboard.name == neoclip.driver.id() then
        h.ok(string.format("*%s* driver is in use", neoclip.driver.id()))

        local display = nil
        if vim.endswith(neoclip.driver.id(), "Wayland") then
            display = vim.env.WAYLAND_DISPLAY
        elseif vim.endswith(neoclip.driver.id(), "X11") then
            display = vim.env.DISPLAY
        end
        if display then
            h.info(string.format("Running on display `%s`", display))
        end
    else
        h.error(string.format("*%s* driver is loaded but not registered",
            neoclip.driver.id()), {
            "Do not install another clipboard provider",
            "Check list of issues",
            "Try |neoclip:register()|"
        })
    end

    if not neoclip.driver.status() then
        h.warn("Driver module stopped", {
            "|neoclip.driver.start()| to restart"
        })
    end

    if #neoclip.issues > 0 then
        h.warn("Found issues", neoclip.issues)
    end

    local plus, star = vim.fn.getreginfo"+", vim.fn.getreginfo"*"
    neoclip.driver.set("+", { "На дворе трава" }, "b")
    neoclip.driver.set("*", { "На траве дрова" }, "b")
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