source("config.lua")

spawn("swaybg", "-m", "fill", "-i", env.WALLPAPER)
spawn("swaync")
spawn("blueman-applet")
spawn("1password", "--silent")

spawn("waybar")
config.grid.pad.bottom = 4 + config.border.width
