source("config.lua")

-- Clients ---------------------------------------------------------------------

spawn("dunst")
spawn("blueman-applet")
spawn("1password", "--silent")
spawn("waybar")
spawn("playerctld")
spawn("wl-paste", "--watch", "cliphist", "store")

-- Outputs ---------------------------------------------------------------------

config.background.image = env.WALLPAPER

config.output.on_add_or_remove = function(output, added)
    spawn("wlr-randr", "--output", "DP-1", "--adaptive-sync", "disabled", "--output", "DP-2", "--left-of", "DP-1")
end

config.bind["Mod+p"]       = function() spawn("wlr-randr", "--output", "DP-1", "--adaptive-sync", "enabled")  end
config.bind["Mod+Shift+P"] = function() spawn("wlr-randr", "--output", "DP-1", "--adaptive-sync", "disabled") end
