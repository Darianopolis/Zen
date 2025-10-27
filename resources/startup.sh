alias m='zen msg'

source_dir=$(dirname -- "$(readlink -f -- "$BASH_SOURCE")")

sh $source_dir/conf.sh

m spawn waybar
m spawn swaybg -m fill -i $WALLPAPER
m spawn swaync
