#!/bin/sh

set -e

python setup.py sdist build
cp dist/*.tar.gz podman/testdata

for distro in arch fedora debian ubuntu
do
    podman build -t gerbolyze-$distro-testenv -f podman/$distro-testenv
    mkdir -p /tmp/gerbolyze-test-out
    podman run --mount type=bind,src=podman/testdata,dst=/data,ro --mount type=bind,src=/tmp/gerbolyze-test-out,dst=/out gerbolyze-$distro-testenv /data/testscript.sh
done

