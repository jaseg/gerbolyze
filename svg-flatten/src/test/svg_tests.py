#!/usr/bin/env python3

import tempfile
import unittest
from pathlib import Path
import subprocess
import os

from PIL import Image
import numpy as np

def run_svg_flatten(input_file, output_file, **kwargs):
    if 'SVG_FLATTEN' in os.environ:
        svg_flatten = os.environ.get('SVG_FLATTEN')
    elif (Path(__file__) / '../../build/svg-flatten').is_file():
        svg_flatten = '../../build/svg-flatten'
    elif Path('./build/svg-flatten').is_file():
        svg_flatten = './build/svg-flatten'
    else:
        svg_flatten = 'svg-flatten'

    args = [ svg_flatten,
            *(arg for (key, value) in kwargs.items() for arg in (f'--{key.replace("_", "-")}', value)),
            str(input_file), str(output_file) ]

    try:
        proc = subprocess.run(args, capture_output=True, check=True)
    except:
        print('Subprocess stdout:')
        print(proc.stdout)
        print('Subprocess stderr:')
        print(proc.stderr)
        raise

class SVGRoundTripTests(unittest.TestCase):

    def compare_images(self, reference, output, test_name, mean=0.01):
        ref = np.array(Image.open(reference))
        out = np.array(Image.open(output))
        delta = np.abs(out - ref).astype(float) / 255

        #print(f'{test_name}: mean={delta.mean():.5g}')

        self.assertTrue(delta.mean() < mean,
                f'Expected mean pixel difference between images to be <{mean}, was {delta.mean():.5g}')

    def run_svg_round_trip_test(self, test_in_svg):
        with tempfile.NamedTemporaryFile(suffix='.svg') as tmp_out_svg,\
            tempfile.NamedTemporaryFile(suffix='.png') as tmp_out_png,\
            tempfile.NamedTemporaryFile(suffix='.png') as tmp_in_png:

            run_svg_flatten(test_in_svg, tmp_out_svg.name, format='svg')

            subprocess.run(['resvg', tmp_out_svg.name, tmp_out_png.name], check=True, stdout=subprocess.DEVNULL)
            subprocess.run(['resvg', test_in_svg, tmp_in_png.name], check=True, stdout=subprocess.DEVNULL)

            try:
                self.compare_images(tmp_in_png, tmp_out_png, test_in_svg.stem)
            except AssertionError as e:
                import shutil
                shutil.copyfile(tmp_in_png.name, f'/tmp/gerbolyze-fail-{test_in_svg.stem}-in.png')
                shutil.copyfile(tmp_out_png.name, f'/tmp/gerbolyze-fail-{test_in_svg.stem}-out.png')
                foo = list(e.args)
                foo[0] += '\nFailing test renderings copied to:\n'
                foo[0] += f'  /tmp/gerbolyze-fail-{test_in_svg.stem}-{{in|out}}.png\n'
                e.args = tuple(foo)
                raise e

for test_in_svg in Path('testdata/svg').glob('*.svg'):
    # We need to make sure we capture the loop variable's current value here.
    gen = lambda testcase: lambda self: self.run_svg_round_trip_test(testcase)
    setattr(SVGRoundTripTests, f'test_{test_in_svg.stem}', gen(test_in_svg))

if __name__ == '__main__':
    unittest.main()
