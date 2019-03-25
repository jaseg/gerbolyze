#!/usr/bin/env python3

from setuptools import setup

def readme():
    with open('README.rst') as f:
        return f.read()

setup(
    name = 'gerbolyze',
    version = '0.1.9',
    py_modules = ['gerbolyze'],
    scripts = ['gerbolyze'],
    description = ('A high-resolution image-to-PCB converter. Gerbolyze reads and vectorizes black-and-white raster '
        'images, then plots the vectorized image into an existing gerber file while avoiding existing features such as '
        'text or holes.'),
    long_description=readme(),
    long_description_content_type='text/x-rst',
    url = 'https://github.com/jaseg/gerbolyze',
    author = 'jaseg',
    author_email = 'github@jaseg.net',
    install_requires = ['pcb-tools', 'tqdm', 'numpy', 'opencv-python'],
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
