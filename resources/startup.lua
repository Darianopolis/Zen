source("config.lua")

spawn("waybar")
spawn("swaybg", "-m", "fill", "-i", env.WALLPAPER)
spawn("swaync")
spawn("blueman-applet")
spawn("1password", "--silent")
