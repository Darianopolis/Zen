source("config.lua")

-- Clients ---------------------------------------------------------------------

spawn("swaync")
spawn("blueman-applet")
spawn("1password", "--silent")

spawn("waybar")
config.grid.pad.bottom = 4 + config.border.width

-- Outputs ---------------------------------------------------------------------

config.background.image = env.WALLPAPER

config.output.on_add_or_remove = function(output, added)
    spawn("wlr-randr", "--output", "DP-1", "--adaptive-sync", "disabled", "--output", "DP-2", "--left-of", "DP-1")
end

config.bind["Mod+p"]       = function() spawn("wlr-randr", "--output", "DP-1", "--adaptive-sync", "enabled")  end
config.bind["Mod+Shift+P"] = function() spawn("wlr-randr", "--output", "DP-1", "--adaptive-sync", "disabled") end
