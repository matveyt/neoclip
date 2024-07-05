--[[
    neoclip - Neovim clipboard provider
    Last Change:    2024 Jul 03
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

    if vim.fn.has"clipboard" == 0 then
        h.error("Clipboard provider is disabled", {
            "Registers + and * will not work",
        })
        return
    end

    if not neoclip then
        h.error("Not found", {
            "Do you |require()| it?",
            "Refer to |neoclip-build| on how to build from source code",
            "OS Windows, macOS and *nix (with X11 or Wayland) are supported",
        })
        return
    end

    if not neoclip.driver then
        h.error("No driver module loaded", neoclip.issues or {
            "Have you run |neoclip:setup()|?",
        })
        return
    end

    if neoclip.driver.id() == vim.fn["provider#clipboard#Executable"]() then
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
        h.warn(string.format("*%s* driver is loaded but not properly registered",
            neoclip.driver.id()), {
            "Do not install another clipboard provider",
            "Check list of issues",
            "Try |neoclip:register()|",
        })
    end

    if not neoclip.driver.status() then
        h.warn("Driver module stopped", {
            "|neoclip.driver.start()| to restart",
        })
    end

    if #neoclip.issues > 0 then
        h.warn("Found issues", neoclip.issues)
    end

    local reg_plus, reg_star = vim.fn.getreginfo"+", vim.fn.getreginfo"*"
    local line_plus, line_star = "На дворе трава", "На траве дрова"
    local now = tostring(os.time())

    if not neoclip.driver.set("+", { now, line_plus }, "b") then
        h.warn"Driver failed to set register +"
    end
    if not neoclip.driver.set("*", { now, line_star }, "b") then
        h.warn"Driver failed to set register *"
    end

    -- for "cache_enabled" provider
    vim.cmd"sleep 200m"

    local test_plus = vim.fn.getreginfo"+"
    vim.fn.setreg("+", reg_plus)
    vim.fn.setreg("*", reg_star)

    if #test_plus.regcontents == 2 and test_plus.regcontents[1] == now and
        (test_plus.regcontents[2] == line_plus or test_plus.regcontents[2] == line_star)
        then
        h.ok"Clipboard test passed"

        if test_plus.regcontents[2] == line_star then
            h.info"NOTE registers + and * are always equal"
        end

        assert(#line_plus == #line_star and #line_plus >= #now)
        if test_plus.regtype ~= "\22" .. #line_plus then
            h.warn(string.format("Block type has been changed to %q", test_plus.regtype),
            {
                string.format("It looks like %s does not support Vim's native blocks",
                    vim.fn["provider#clipboard#Executable"]()),
            })
        end
    else
        h.error("Clipboard test failed", {
            "Sometimes, this happens because of `cache_enabled` setting",
            "Repeat |:checkhealth| again before reporting a bug",
        })
    end
end


return { check = neo_check }
