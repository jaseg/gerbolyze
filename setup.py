#!/usr/bin/env python3

from setuptools import setup

def readme():
    with open('README.rst') as f:
        return f.read()

setup(
    name = 'gerbolyze',
    version = '2.0.0',
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
