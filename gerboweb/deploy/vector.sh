#!/bin/sh

[ $# != 2 ] && exit 1
ID=$1
egrep -x -q '^[-0-9A-Za-z]{36}$'<<<"$ID" || exit 2
LAYER=$2
egrep -x -q '^(top|bottom)$'<<<"$LAYER" || exit 2

systemd-nspawn \
    -D /var/cache/gerbolyze_container \
    -x --bind=/var/cache/gerboweb/upload/$ID:/mnt \
    /bin/sh -c "set -euo pipefail
cd /tmp
unzip -j -d gerber_in /mnt/gerber.zip
gerbolyze vectorize $LAYER gerber_in gerber /mnt/overlay.png
rm -f /mnt/gerber_out.zip
zip -r /mnt/gerber_out.zip gerber"

