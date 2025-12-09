source("config.lua")

-- XWayland --------------------------------------------------------------------

xwayland(nil, function(socket) env.DISPLAY = socket end)

-- Clients ---------------------------------------------------------------------

spawn("waybar")
spawn("wl-paste", "--watch", "cliphist", "store")

-- Outputs ---------------------------------------------------------------------

config.background.image = env.WALLPAPER
