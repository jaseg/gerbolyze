#!/bin/sh

pip3 install --user /data/gerbolyze-*.tar.gz --no-binary gerbolyze
/root/.local/bin/svg-flatten --clear-color black --dark-color white --format svg /data/test_svg_readme.svg /out/test_out.svg

