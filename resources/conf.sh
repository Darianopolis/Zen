alias m='zen msg'

source_dir=$(dirname -- "$(readlink -f -- "$BASH_SOURCE")")

# audio control

m bind "XF86AudioLowerVolume" spawn wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.01-
m bind "XF86AudioRaiseVolume" spawn wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.01+
m bind "XF86AudioMute"        spawn wpctl set-volume @DEFAULT_AUDIO_SINK@ toggle

# playerctl

PLAYER="spotify"
m bind "XF86AudioPlay" spawn playerctl -p $PLAYER play-pause
m bind "XF86AudioPrev" spawn playerctl -p $PLAYER prev
m bind "XF86AudioNext" spawn playerctl -p $PLAYER next

# launcher

m bind "Mod+d"       spawn rofi -show drun
m bind "Mod+Shift+D" spawn rofi -show run
m bind "Mod+Ctrl+d"  spawn rofi -show window

# applications

m bind "Mod+t"       spawn konsole
m bind "Mod+Shift+T" spawn konsole --workdir $(pwd)
m bind "Mod+g"       spawn dolphin
m bind "Mod+h"       spawn kalk

# managers

m bind "Mod+v" spawn pavucontrol
m bind "Mod+b" spawn blueman-manager
m bind "Mod+g" spawn swaync-client --toggle-panel

# capture

m bind "Print" spawn sh $source_dir/screenshot.sh

# system

m bind "Mod+n" spawn systemctl suspend

# debug

m bind "Mod+i" spawn xeyes
