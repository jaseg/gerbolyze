# -*- coding: utf-8 -*-
#
# Copyright 2022 Jan Götte <code@jaseg.de>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import sys
import subprocess
import tempfile
from pathlib import Path

import gerbonara
import pytest


REFERENCE_GERBERS = ['test_gerber_8seg.zip']
REFERENCE_SVGS = ['svg_feature_test.svg']

reference_path = lambda reference: Path(__file__).parent / 'resources' / str(reference)


def run_command(*args):
    try:
        proc = subprocess.run(args, check=True, capture_output=True)
        print(proc.stdout.decode())
        print(proc.stderr.decode(), file=sys.stderr)
    except subprocess.CalledProcessError as e:
        print(e.stdout.decode())
        print(e.stderr.decode(), file=sys.stderr)
        raise

def test_template_round_trip():
    with tempfile.NamedTemporaryFile(suffix='.svg') as out_svg,\
            tempfile.TemporaryDirectory() as out_dir:
        run_command('python3', '-m', 'gerbolyze', 'empty-template', '--force', out_svg.name)
        run_command('python3', '-m', 'gerbolyze', 'convert', out_svg.name, out_dir)

def test_zip_write():
    with tempfile.NamedTemporaryFile(suffix='.svg') as out_svg,\
            tempfile.NamedTemporaryFile(suffix='.zip') as out_zip:
        run_command('python3', '-m', 'gerbolyze', 'empty-template', '--force', out_svg.name)
        run_command('python3', '-m', 'gerbolyze', 'convert', out_svg.name, out_zip.name)

@pytest.mark.parametrize('reference', REFERENCE_SVGS)
def test_complex_conversion(reference):
    infile = reference_path(reference)
    with tempfile.NamedTemporaryFile(suffix='.zip') as out_zip:
        run_command('python3', '-m', 'gerbolyze', 'convert', infile, out_zip.name)
        run_command('python3', '-m', 'gerbolyze', 'convert', '--pattern-complete-tiles-only', '--use-apertures-for-patterns', infile, out_zip.name)

@pytest.mark.parametrize('reference', REFERENCE_GERBERS)
def test_template(reference):
    with tempfile.NamedTemporaryFile(suffix='.zip') as out_svg:
        infile = reference_path(reference)
        run_command('python3', '-m', 'gerbolyze', 'template', '--top', '--force', infile, out_svg.name)
        run_command('python3', '-m', 'gerbolyze', 'template', '--bottom', '--force', '--vector', infile, out_svg.name)

def test_convert_layers():
    infile = reference_path('layers.svg')
    with tempfile.TemporaryDirectory() as out_dir:
        run_command('python3', '-m', 'gerbolyze', 'convert', infile, out_dir)
        stack = gerbonara.layers.LayerStack.open(out_dir)

        for layer, dia in {
                'top paste':        0.100,
                'top silk':         0.110,
                'top mask':         0.120,
                'top copper':       0.130,
                'bottom copper':    0.140,
                'bottom mask':      0.150,
                'bottom silk':      0.160,
                'bottom paste':     0.170,
                'mechanical outline':    0.09}.items():
            assert set(round(ap.diameter, 4) for ap in stack[layer].apertures) == {dia, 0.05}

        # Note: svg-flatten rounds these diameters to the geometric tolerance given on the command line (0.01mm by
        # default).
        assert stack.drill_pth.drill_sizes() == [0.7]
        assert stack.drill_npth.drill_sizes() == [0.5]

