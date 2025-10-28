alias m="$1 msg"

source_dir=$(dirname -- "$(readlink -f -- "$BASH_SOURCE")")

sh $source_dir/conf.sh $1

m spawn waybar
m spawn swaybg -m fill -i $WALLPAPER
m spawn swaync
