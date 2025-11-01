source("conf.lua")

spawn("waybar")
spawn("swaybg", "-m", "fill", "-i", env.get("WALLPAPER"))
spawn("swaync")
spawn("blueman-applet")
spawn("1password", "--silent")
