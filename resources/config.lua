
-- layout ----------------------------------------------------------------------

config.focus_cycle.opacity = 0.2

config.border.width = 2
config.border.radius = 2
config.border.color.default = "#4C4C4C"
config.border.color.focused = "#6666FF"

config.grid.width   = 6
config.grid.height  = 2
config.grid.pad.inner  = 4 + config.border.width * 2
config.grid.pad.left   = 7 + config.border.width
config.grid.pad.top    = 7 + config.border.width
config.grid.pad.right  = 7 + config.border.width
config.grid.pad.bottom = 7 + config.border.width
config.grid.color.initial  = "#99999966"
config.grid.color.selected = "#6666FF66"
config.grid.leeway.horizontal = 200
config.grid.leeway.vertical   = 200

-- audio control ---------------------------------------------------------------

config.bind["XF86AudioLowerVolume"] = function() spawn("wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.01-")  end
config.bind["XF86AudioRaiseVolume"] = function() spawn("wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.01+")  end
config.bind["XF86AudioMute"]        = function() spawn("wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "toggle") end

-- playerctl -------------------------------------------------------------------

local player = "spotify"
config.bind["XF86AudioPlay"] = function() spawn("playerctl", "-p", player, "play-pause") end
config.bind["XF86AudioPrev"] = function() spawn("playerctl", "-p", player, "previous")   end
config.bind["XF86AudioNext"] = function() spawn("playerctl", "-p", player, "next")       end

-- launcher --------------------------------------------------------------------

config.bind["Mod+d"]       = function() spawn("rofi", "-show-icons", "-show", "drun")   end
config.bind["Mod+Shift+D"] = function() spawn("rofi", "-show-icons", "-show", "run")    end
config.bind["Mod+Ctrl+d"]  = function() spawn("rofi", "-show-icons", "-show", "window") end

-- applications ----------------------------------------------------------------

config.bind["Mod+t"]       = function() spawn("konsole") end
config.bind["Mod+Shift+T"] = function() spawn("konsole", "--workdir", process.cwd) end
config.bind["Mod+g"]       = function() spawn("dolphin") end
config.bind["Mod+h"]       = function() spawn("kalk")    end

-- managers --------------------------------------------------------------------

config.bind["Mod+v"] = function() spawn("pavucontrol")     end
config.bind["Mod+b"] = function() spawn("blueman-manager") end
config.bind["Mod+j"] = function() spawn("swaync-client", "--toggle-panel") end

-- capture ---------------------------------------------------------------------

config.bind["Print"] = function() spawn("sh", "-c", "grim -g \"$(slurp)\" - | wl-copy") end

-- system ----------------------------------------------------------------------

config.bind["Mod+n"] = function() spawn("systemctl", "suspend") end

-- debug -----------------------------------------------------------------------

config.bind["Mod+i"] = function() spawn("xeyes")     end
config.bind["Mod+o"] = function() debug.output.new() end
config.bind["Mod+k"] = function() debug.cursor = not debug.cursor end
