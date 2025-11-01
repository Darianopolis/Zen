
-- audio control

bind("XF86AudioLowerVolume", function() spawn("wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.01-")  end)
bind("XF86AudioRaiseVolume", function() spawn("wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "0.01+")  end)
bind("XF86AudioMute",        function() spawn("wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "toggle") end)

-- playerctl

local player = "spotify"
bind("XF86AudioPlay", function() spawn("playerctl", "-p", player, "play-pause") end)
bind("XF86AudioPrev", function() spawn("playerctl", "-p", player, "previous")   end)
bind("XF86AudioNext", function() spawn("playerctl", "-p", player, "next")       end)

-- launcher

bind("Mod+d",       function() spawn("rofi", "-show", "drun")   end)
bind("Mod+Shift+D", function() spawn("rofi", "-show", "run")    end)
bind("Mod+Ctrl+d",  function() spawn("rofi", "-show", "window") end)

-- applications

bind("Mod+t",       function() spawn("konsole")                              end)
bind("Mod+Shift+T", function() spawn("konsole", "--workdir", env.launch_dir) end)
bind("Mod+g",       function() spawn("dolphin")                              end)
bind("Mod+h",       function() spawn("kalk")                                 end)

-- managers

bind("Mod+v", function() spawn("pavucontrol")                     end)
bind("Mod+b", function() spawn("blueman-manager")                 end)
bind("Mod+j", function() spawn("swaync-client", "--toggle-panel") end)

-- capture

bind("Print", function() spawn("sh", "-c", "grim -g \"$(slurp)\" - | wl-copy") end)

-- system

bind("Mod+n", function() spawn("systemctl", "suspend") end)

-- debug

bind("Mod+i",  function() spawn("xeyes")               end)
bind("Mod+k",  function() debug.cursor("toggle")       end)
bind("Mod+o^", function() debug.output.new()           end)
bind("Mod+u",  function() debug.stats.window("toggle") end)
bind("Mod+y",  function() debug.stats.output("toggle") end)
