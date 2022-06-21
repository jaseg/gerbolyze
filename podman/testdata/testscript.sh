#!/bin/sh

set -e 
rsync -a /data/git/ git/
cd git

git config --global --add safe.directory $(realpath git)

cp svg-flatten/build/svg-flatten.wasm svg-flatten/svg_flatten_wasi/
cd svg-flatten
python3 setup.py install
cd ..

pip install --upgrade --no-cache-dir 'gerbonara>=0.11.0'
python3 setup.py install

export WASMTIME_BACKTRACE_DETAILS=1
python3 -m pytest $@
