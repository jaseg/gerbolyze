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


def get_virtualenv_path():
    """Used to work out path to install compiled binaries to."""
    if hasattr(sys, 'real_prefix'):
        return sys.prefix

    if hasattr(sys, 'base_prefix') and sys.base_prefix != sys.prefix:
        return sys.prefix

    if 'conda' in sys.prefix:
        return sys.prefix

    return '/usr/local'

def compile_and_install_svgflatten():
    src_path = 'svg-flatten'

    try:
        subprocess.run(['make', 'check-deps'], cwd=src_path, check=True)
        subprocess.run(['make', '-j', str(cpu_count()), 'all'], cwd=src_path, check=True)
        subprocess.run(['make', 'install', f'PREFIX={get_virtualenv_path()}'], cwd=src_path, check=True)
    except subprocess.CalledProcessError:
        print('Error building svg-flatten C++ binary. Please see log above for details.', file=sys.stderr)
        sys.exit(1)

def has_usvg():
    checks = ['usvg', str(Path.home() / '.cargo' / 'bin' / 'usvg')]
    if 'USVG' in os.environ:
        checks = [os.environ['USVG'], *checks]

    for check in checks:
        try:
            subprocess.run(['usvg'], capture_output=True)
            return True

        except FileNotFoundError:
            pass

    else:
        return False

def install_usvg():
    try:
        subprocess.run(['cargo'], check=True, capture_output=True)

    except subprocess.CalledProcessError as e:
        if b'no default toolchain set' in e.stderr:
            print('No rust installation found. Calling rustup.')

            try:
                subprocess.run(['rustup', 'install', 'stable'], check=True)
                subprocess.run(['rustup', 'default', 'stable'], check=True)

            except subprocess.FileNotFoundError as e:
                print('Cannot find rustup executable. svg-flatten needs usvg, which we install via rustup. Please install rustup or install usvg manually.', file=sys.stderr)
                sys.exit(1)

            except subprocess.CalledProcessError as e:
                print('Error installing usvg:', e.returncode, file=sys.stderr)
                sys.exit(1)

        else:
            print('Error installing usvg:', e.returncode, file=sys.stderr)
            print(e.stdout.decode())
            print(e.stderr.decode())
            sys.exit(1)

    except subprocess.FileNotFoundError as e:
        print('Cannot find cargo executable. svg-flatten needs usvg, which we install via cargo. Please install cargo or install usvg manually.', file=sys.stderr)
        sys.exit(1)

    try:
        subprocess.run(['cargo', 'install', 'usvg'], check=True)

    except subprocess.CalledProcessError as e:
        print('Error installing usvg:', e.returncode, file=sys.stderr)
        sys.exit(1)

class CustomInstall(install):
    """Custom handler for the 'install' command."""
    def run(self):
        compile_and_install_svgflatten()
        if not has_usvg():
            print('usvg not found. Installing.')
            install_usvg()
        super().run()

setup(
    cmdclass={'install': CustomInstall},
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

