#!/bin/sh

[ $# != 1 ] && exit 1
ID=$1
egrep -x -q '^[-0-9A-Za-z]{36}$'<<<"$ID" || exit 2

systemd-nspawn \
    -D /var/cache/gerbolyze_container \
    -x --bind=/var/cache/gerboweb/upload/$ID:/mnt \
    /bin/sh -c "set -euo pipefail
unzip -j -d /tmp/gerber /mnt/gerber.zip
rm -f /mnt/render_top.png /mnt/render_bottom.png /mnt/render_top.small.png /mnt/render_bottom.small.png
date; echo 'Rendering bottom layer'
gerbolyze render top /tmp/gerber /mnt/render_top.png
date; echo 'Scaling down'
convert /mnt/render_top.png -resize 500x500 /mnt/render_top.small.png
date; echo 'Rendering top layer'
gerbolyze render bottom /tmp/gerber /mnt/render_bottom.png
date; echo 'Scaling down'
convert /mnt/render_bottom.png -resize 500x500 /mnt/render_bottom.small.png"
