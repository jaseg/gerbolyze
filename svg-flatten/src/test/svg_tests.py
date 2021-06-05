#!/usr/bin/env python3

import tempfile
import shutil
import unittest
from pathlib import Path
import subprocess
import os

from PIL import Image
import numpy as np

def run_svg_flatten(input_file, output_file, *args, **kwargs):
    if 'SVG_FLATTEN' in os.environ:
        svg_flatten = os.environ.get('SVG_FLATTEN')
    elif (Path(__file__) / '../../build/svg-flatten').is_file():
        svg_flatten = '../../build/svg-flatten'
    elif Path('./build/svg-flatten').is_file():
        svg_flatten = './build/svg-flatten'
    else:
        svg_flatten = 'svg-flatten'

    args = [ svg_flatten ]
    for key, value in kwargs.items():
        key = '--' + key.replace("_", "-")
        args.append(key)

        if type(value) is not bool:
            args.append(value)
    args.append(str(input_file))
    args.append(str(output_file))

    try:
        proc = subprocess.run(args, capture_output=True, check=True)
    except:
        print('Subprocess stdout:')
        print(proc.stdout)
        print('Subprocess stderr:')
        print(proc.stderr)
        raise

def run_cargo_cmd(cmd, args, **kwargs):
    if cmd.upper() in os.environ:
        return subprocess.run([os.environ[cmd.upper()], *args], **kwargs)

    try:
        return subprocess.run([cmd, *args], **kwargs)

    except FileNotFoundError:
        return subprocess.run([str(Path.home() / '.cargo' / 'bin' / cmd), *args], **kwargs)

class SVGRoundTripTests(unittest.TestCase):

    # Notes on test cases:
    # Our stroke join test shows a discrepancy in miter handling between resvg and gerbolyze. Gerbolyze's miter join is
    # the one from Clipper, which unfortunately cannot be configured. resvg uses one looking like that from the SVG 2
    # spec. Gerbolyze's join is legal by the 1.1 spec since this spec does not explicitly define the miter offset. It
    # only contains an unclear picture, and that picture looks approximately like what gerbolyze produces.

    test_mean_default = 0.02
    test_mean_overrides = {
        # Both of these produce high errors for two reasons:
        # * By necessity, we get some error accumulation because we are dashing the path *after* flattening, and
        #   flattened path length is always a tiny bit smaller than actual path length for curved paths.
        # * Since the image contains a lot of edges there are lots of small differences in anti-aliasing. 
        # Both are expected and OK.
        'stroke_dashes_comparison': 0.03,
        'stroke_dashes': 0.05,
        # The vectorizer tests produce output with lots of edges, which leads to a large amount of aliasing artifacts.
        'vectorizer_simple': 0.05,
        'vectorizer_clip': 0.05,
        'vectorizer_xform': 0.05,
        'vectorizer_xform_clip': 0.05,
    }

    # Force use of rsvg-convert instead of resvg for these test cases
    rsvg_override = {
        # resvg is bad at rendering patterns. Both scale and offset are wrong, and the result is a blurry mess.
        # See https://github.com/RazrFalcon/resvg/issues/221
        'pattern_fill',
        'pattern_stroke',
        'pattern_stroke_dashed'
    }

    def compare_images(self, reference, output, test_name, mean, vectorizer_test=False, rsvg_workaround=False):
        ref, out = Image.open(reference), Image.open(output)

        if vectorizer_test:
            target_size = (100, 100)
            ref.thumbnail(target_size, Image.ANTIALIAS)
            out.thumbnail(target_size, Image.ANTIALIAS)
            ref, out = np.array(ref), np.array(out)

        else:
            ref, out = np.array(ref), np.array(out)

        ref, out = ref.astype(float).mean(axis=2), out.astype(float).mean(axis=2)


        if rsvg_workaround:
            # For some stupid reason, rsvg-convert does not actually output black as in "black" pixels when asked to.
            # Instead, it outputs #010101. We fix this in post here.
            ref = (ref - 1.0) * (255/254)
        delta = np.abs(out - ref).astype(float) / 255


        #def print_stats(ref):
        #    print('img:', ref.min(), ref.mean(), ref.max(), 'std:', ref.std(), 'channels:', *(ref[:,:,i].mean() for i in
        #            range(ref.shape[2])))
        #print_stats(ref)
        #print_stats(out)

        # print(f'{test_name}: mean={delta.mean():.5g}')

        # self.fail('debug')
        self.assertTrue(delta.mean() < mean,
                f'Expected mean pixel difference between images to be <{mean}, was {delta.mean():.5g}')

    def run_svg_round_trip_test(self, test_in_svg):
        with tempfile.NamedTemporaryFile(suffix='.svg') as tmp_out_svg,\
            tempfile.NamedTemporaryFile(suffix='.png') as tmp_out_png,\
            tempfile.NamedTemporaryFile(suffix='.png') as tmp_in_png:

            use_rsvg = test_in_svg.stem in SVGRoundTripTests.rsvg_override
            vectorizer_test = test_in_svg.stem.startswith('vectorizer')
            contours_test = test_in_svg.stem.startswith('contours')

            if not vectorizer_test:
                run_svg_flatten(test_in_svg, tmp_out_svg.name, format='svg')

            else:
                run_svg_flatten(test_in_svg, tmp_out_svg.name, format='svg',
                        svg_white_is_gerber_dark=True,
                        clear_color='black', dark_color='white')

            if contours_test:
                run_svg_flatten(test_in_svg, tmp_out_svg.name, 
                        clear_color='black', dark_color='white',
                        svg_white_is_gerber_dark=True,
                        format='svg',
                        vectorizer='binary-contours')

            if not use_rsvg: # default!
                run_cargo_cmd('resvg', [tmp_out_svg.name, tmp_out_png.name], check=True, stdout=subprocess.DEVNULL)
                run_cargo_cmd('resvg', [test_in_svg, tmp_in_png.name], check=True, stdout=subprocess.DEVNULL)

            else:
                subprocess.run(['rsvg-convert', tmp_out_svg.name, '-f', 'png', '-o', tmp_out_png.name], check=True, stdout=subprocess.DEVNULL)
                subprocess.run(['rsvg-convert', test_in_svg, '-f', 'png', '-o', tmp_in_png.name], check=True, stdout=subprocess.DEVNULL)

            try:
                self.compare_images(tmp_in_png, tmp_out_png, test_in_svg.stem,
                        SVGRoundTripTests.test_mean_overrides.get(test_in_svg.stem, SVGRoundTripTests.test_mean_default),
                        vectorizer_test, rsvg_workaround=use_rsvg)

            except AssertionError as e:
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
