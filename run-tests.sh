#!/bin/sh

set -e

while [ $# -gt 0 ]; do
    case $1 in
        --parallel)
            CONTAINER_ARGS="--workers auto $CONTAINER_ARGS"
            shift;;
        -x)
            CONTAINER_ARGS="-x $CONTAINER_ARGS"
            shift;;
        --no-cache)
            NO_CACHE=--no-cache
            shift;;
        *)
            echo "Unknown argument \"$1\""
            exit 1
            shift;;
    esac
done

mkdir -p podman/testdata/git
git ls-tree --full-tree -r HEAD --name-only | rsync -lptgoDv --delete . --files-from - podman/testdata/git/
#git clone --depth 1 . podman/testdata/git

for distro in ubuntu-old ubuntu arch
do
    podman build $NO_CACHE -t gerbonara-$distro-testenv -f podman/$distro-testenv
    mkdir -p /tmp/gerbonara-test-out
    podman run --mount type=bind,src=podman/testdata,dst=/data,ro --mount type=bind,src=/tmp/gerbonara-test-out,dst=/out gerbonara-$distro-testenv /data/testscript.sh $CONTAINER_ARGS
done

