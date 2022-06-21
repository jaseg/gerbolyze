#!/bin/sh

set -e 
rsync -av /data/git git
cd git

python3 -m pytest $@
