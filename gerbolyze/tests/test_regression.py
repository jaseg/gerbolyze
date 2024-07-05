#s -*- coding: utf-8 -*-
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
import math
import subprocess
import tempfile
from pathlib import Path
from xml.etree import ElementTree

import gerbonara
import pytest

from .test_integration import run_command



def test_template_round_trip():
    r = 50 # mm
    n_points = 500000

    with tempfile.NamedTemporaryFile(suffix='.svg') as out_svg,\
            tempfile.NamedTemporaryFile(suffix='.svg') as proc_svg,\
            tempfile.TemporaryDirectory() as out_dir:
            run_command('python3', 'gerbolyze', 'empty-template', '--force', '--size', f'{2*(r)}x{2*(r)}mm', out_svg.name)

            ElementTree.register_namespace('', 'http://www.w3.org/2000/svg')
            ElementTree.register_namespace('svg', 'http://www.w3.org/2000/svg')
            et = ElementTree.parse(out_svg)
            for layer in [
                    et.find(".//{http://www.w3.org/2000/svg}g[@id='g-top-copper']"),
                    et.find(".//{http://www.w3.org/2000/svg}g[@id='g-bottom-copper']")]:
                poly = ElementTree.SubElement(layer, '{http://www.w3.org/2000/svg}polygon')
                # Generate n_points points on a circle
                poly.set('points', ' '.join([f'{r + r*math.sin(t)}, {r + r*math.cos(t)}'
                                             for t in (i/n_points * 2*math.pi for i in range(n_points))]))

            et.write(proc_svg)
            proc_svg.flush()

            run_command('python3', 'gerbolyze', 'convert', proc_svg.name, out_dir)
            out_dir = Path(out_dir)

            excellon_files = [f.stat().st_size for f in out_dir.glob('*.drl')]
            print('Excellon file sizes:', excellon_files)

            gerber_files = {f: f.stat().st_size for f in out_dir.glob('*.gbr')}
            print('Gerber file sizes:', gerber_files)

            assert len(excellon_files) == 2
            assert all(20 < x < 100 for x in excellon_files)

            for f, size in gerber_files.items():
                _name, _, layer = f.stem.rpartition('-')
                if layer in ('F.Cu', 'B.Cu'):
                    # These layers should contain a very large G36 polygon
                    assert 10e6 < size < 100e6
                else:
                    # These layers should not contain anything
                    assert 20 < size < 1000

