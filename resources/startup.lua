source("config.lua")

-- XWayland --------------------------------------------------------------------

xwayland(nil, function(socket) env.DISPLAY = socket end)
xwayland(nil, function(socket) env.ALT_DISPLAY = socket end)

-- Clients ---------------------------------------------------------------------

spawn("dunst")
spawn("blueman-applet")
spawn("waybar")
spawn("playerctld")
spawn("wl-paste", "--watch", "cliphist", "store")

-- Outputs ---------------------------------------------------------------------

config.background.image = env.WALLPAPER

local primary   = "DP-1"
local secondary = "DP-2"

config.output.on_add_or_remove = function(output, added)
    spawn("wlr-randr", "--output", secondary, "--left-of", primary)
end

config.bind["Mod+p"]       = function() spawn("wlr-randr", "--output", primary, "--adaptive-sync", "enabled")  end
config.bind["Mod+Shift+P"] = function() spawn("wlr-randr", "--output", primary, "--adaptive-sync", "disabled") end
