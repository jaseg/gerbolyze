#!/usr/bin/env python3

import os
import sys
from setuptools import setup
from setuptools.command.install import install
import subprocess
from multiprocessing import cpu_count
from pathlib import Path

def readme():
    with open('README.rst') as f:
        return f.read()

def compile_and_install_svgflatten(target_dir):
    src_path = 'svg-flatten'

    try:
        subprocess.run(['make', 'check-deps'], cwd=src_path, check=True)
        subprocess.run(['make', '-j', str(cpu_count()), 'all'], cwd=src_path, check=True)
        bin_dir = target_dir / ".."
        bin_dir.mkdir(parents=True, exist_ok=True)
        subprocess.run(['make', 'install', f'PREFIX={bin_dir.resolve()}'], cwd=src_path, check=True)
    except subprocess.CalledProcessError:
        print('Error building svg-flatten C++ binary. Please see log above for details.', file=sys.stderr)
        sys.exit(1)

class CustomInstall(install):
    """Custom handler for the 'install' command."""
    def run(self):
        compile_and_install_svgflatten(Path(self.install_scripts))
        super().run()

setup(
    cmdclass={'install': CustomInstall},
    name = 'gerbolyze',
    version = '2.1.1',
    py_modules = ['gerbolyze'],
    package_dir = {'': 'gerbolyze'},
    entry_points = '''
        [console_scripts]
        gerbolyze=gerbolyze:cli
        ''',
    description = ('A high-resolution image-to-PCB converter. Gerbolyze plots SVG, PNG and JPG onto existing gerber '
        'files. It handles almost the full SVG spec and deals with text, path outlines, patterns, arbitrary paths with '
        'self-intersections and holes, etc. fully automatically. It can vectorize raster images both by contour '
        'tracing and by grayscale dithering. All processing is done at the vector level without intermediate '
        'conversions to raster images accurately preserving the input.'),
    long_description=readme(),
    long_description_content_type='text/x-rst',
    url = 'https://git.jaseg.de/gerbolyze',
    author = 'jaseg',
    author_email = 'github@jaseg.de',
    install_requires = ['pcb-tools', 'numpy', 'python-slugify', 'lxml', 'click', 'pcb-tools-extension'],
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

