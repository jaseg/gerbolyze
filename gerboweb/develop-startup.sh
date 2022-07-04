#!/bin/sh

set -e

cd /gerbolyze/gerbonara
echo "### Setting up gerbonara ###"
# newer pip is buggy and just crashes so we pinned an old version.
# python packaging infrastructure is such an incoherent, buggy mess
# also ignore the running pip as root warning, it's dumb and here we actually want to do just that.
python3 -m pip --disable-pip-version-check install .
cd /gerbolyze
echo "### Setting up gerbolyze ###"
python3 -m pip --disable-pip-version-check install .

export PATH=$PATH:$HOME/.cargo/bin
cd /gerbolyze/gerboweb
echo "### Launching app ###"
tmux new-session -d -s dev env GERBOWEB_SETTINGS=gerboweb-develop.cfg FLASK_APP=gerboweb.py flask run -h 0.0.0.0
tmux bind -n C-q kill-session
tmux rename-window gerboweb
tmux split-window -t 0 -v python3 job_processor.py /var/cache/job_queue.sqlite3
tmux attach
