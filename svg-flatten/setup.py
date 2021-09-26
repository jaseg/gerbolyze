import subprocess
from setuptools import setup, find_packages
from pathlib import Path
import re
import shutil

def version():
    res = subprocess.run(['git', 'describe', '--tags', '--match', 'v*'], capture_output=True, check=True, text=True)
    version, _, _rest = res.stdout.strip()[1:].rpartition('-')

def long_description():
    with open("README.rst") as f:
        return f.read()

setup(
    name="svg-flatten-wasi",
    version=version(),
    author="jaseg",
    author_email="pypi@jaseg.de",
    description="svg-flatten SVG downconverter",
    long_description=long_description(),
    long_description_content_type="text/x-rst",
    license="AGPLv3+",
    python_requires="~=3.7",
    setup_requires=["wheel"],
    install_requires=[
        "importlib_resources; python_version<'3.9'",
        "appdirs~=1.4",
        "wasmtime>=0.28",
        "click >= 4.0"
    ],
    packages=["svg_flatten_wasi"],
    package_data={"svg_flatten_wasi": [
        "*.wasm",
    ]},
    entry_points={
        "console_scripts": [
            "wasi-svg-flatten = svg_flatten_wasi:run_svg_flatten",
        ],
    },
    project_urls={
        "Source Code": "https://git.jaseg.de/gerbolyze",
        "Bug Tracker": "https://github.com/jaseg/gerbolyze/issues",
    },
    classifiers=[
        "License :: OSI Approved :: GNU Affero General Public License v3 or later (AGPLv3+)",
    ],
)
