#!/bin/sh

set -e

cd "$(dirname $0)"
podman build -f Containerfile.develop --tag gerbolyze-develop
podman run -p 127.0.0.1:5000:5000 -v ..:/gerbolyze -ti gerbolyze-develop
