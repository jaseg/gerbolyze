#!/usr/bin/env python3

import os
import sys
from setuptools import setup
from setuptools.command.install import install
import subprocess
from multiprocessing import cpu_count
from pathlib import Path
import re

def get_tag():
    res = subprocess.run(['git', 'describe', '--tags', '--match', 'v*'], capture_output=True, check=True, text=True)
    return res.stdout.strip()

def get_version():
    version, _, _rest = get_tag()[1:].partition('-')
    return version

def format_readme_for_pypi():
    tag = get_tag()
    # Replace repo-relative image URLs with gitlab raw URLs. Gitlab and github render repo-relative URLs just fine, but
    # PyPI doesn't.
    return '\n'.join(
            re.sub('^.. (figure|image):: (pics/.*)$', f'.. \\1:: https://gitlab.com/gerbolyze/gerbolyze/-/raw/{tag}/\\2', line.strip('\n'))
            for line in Path('README.rst').read_text().splitlines())

setup(
    name = 'gerbolyze',
    version = get_version(),
    packages=['gerbolyze'],
    scripts=['bin/gerbolyze'],
    description = ('A high-resolution image-to-PCB converter. Gerbolyze plots SVG, PNG and JPG onto existing gerber '
        'files. It handles almost the full SVG spec and deals with text, path outlines, patterns, arbitrary paths with '
        'self-intersections and holes, etc. fully automatically. It can vectorize raster images both by contour '
        'tracing and by grayscale dithering. All processing is done at the vector level without intermediate '
        'conversions to raster images accurately preserving the input.'),
    long_description=format_readme_for_pypi(),
    long_description_content_type='text/x-rst',
    url='https://github.com/jaseg/gerbolyze',
    project_urls={
        'Source Code': 'https://git.jaseg.de/gerbolyze',
        'Bug Tracker': 'https://github.com/jaseg/gerbolyze/issues',
    },
    author = 'jaseg',
    author_email = 'gerbonara@jaseg.de',
    install_requires = ['gerbonara', 'numpy', 'python-slugify', 'lxml', 'click', 'resvg-wasi >= 0.23.0', 'svg-flatten-wasi[resvg-wasi]'],
    license = 'AGPLv3',
    classifiers = [
        'Development Status :: 5 - Production/Stable',
        'Environment :: Console',
        'Intended Audience :: Manufacturing',
        'Intended Audience :: Science/Research',
        'Intended Audience :: Religion',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: GNU Affero General Public License v3 or later (AGPLv3+)',
        'Natural Language :: English',
        'Topic :: Scientific/Engineering :: Electronic Design Automation (EDA)',
        'Topic :: Utilities'
        ]
)

