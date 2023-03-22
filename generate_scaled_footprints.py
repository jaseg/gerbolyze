#/usr/bin/env python3

import re
import tempfile
import os
import subprocess
from pathlib import Path

from bs4 import BeautifulSoup
import click

default_widths = '3mm,5mm,8mm,10mm,12mm,15mm,18mm,20mm,25mm,30mm,35mm,40mm,45mm,50mm,60mm,70mm,80mm,90mm,100mm,120mm,150mm'


# Mostly from https://www.w3.org/TR/css-values/#absolute-lengths
UNIT_FACTORS = {
          'm': 1000,
         'cm': 10,
         'mm': 1,
          'Q': 1/4,
         'in': 25.4,
        'mil': 25.4/1000,
         'pc': 25.4/6,
         'pt': 25.4/72,
         'px': 25.4/96,
    }

def parse_length(foo, default_unit=None):
    ''' Parse given physical length, and return result converted to mm. '''

    match = re.fullmatch(r'(.*?)(m|cm|mm|Q|in|mil|pc|pt|px|)', foo.strip().lower())
    if not match:
        raise ValueError(f'Invalid length "{foo}"')
    num, unit = match.groups()

    if not unit:
        if default_unit:
            unit = default_unit
        else:
            raise ValueError(f'Unit missing from length "{foo}"')

    return float(num) * UNIT_FACTORS[unit]


@click.command()
@click.option('--width')
@click.option('--height')
@click.option('--sexp-layer', default='F.SilkS')
@click.option('--basename', help='Base name for generated symbols and library')
@click.argument('input_svg')
def export(width, height, basename, sexp_layer, input_svg):
    svg_flatten = str(Path(os.environ.get('SVG_FLATTEN', 'svg-flatten')).expanduser())
    usvg = str(Path(os.environ.get('USVG', 'usvg')).expanduser())

    if not basename:
        match = re.fullmatch(r'(.*?)(([-_.][0-9.,]+)(m|cm|mm|Q|in|mil|pc|pt|px|))?', Path(input_svg).stem)
        basename, *rest = match.groups()
        print(f'No --basename given. Using "{basename}"')

    export_width, export_height = width, height
    if not export_width or export_height:
        export_width = default_widths

    elif export_width and export_height:
        raise click.ClickException('Only one of --width or --height must be given.')

    if export_width:
        targets = export_width
        axis = 'width'
    else:
        targets = export_height
        axis = 'height'

    # Determine input document size
    with tempfile.NamedTemporaryFile() as f:
        try:
            subprocess.run([usvg, input_svg, f.name], check=True)
        except FileNotFoundError:
            raise click.ClickException('Cannot find usvg binary in PATH. You can give a custom path to the usvg binary by setting the USVG environment variable.')

        soup = BeautifulSoup(f.read(), features='xml')
        svg = soup.find('svg')
        doc_w_mm, doc_h_mm = parse_length(svg['width'], default_unit='px'), parse_length(svg['height'], default_unit='px')

    print(f'Input file has dimensions width {doc_w_mm:.1f} mm by height {doc_h_mm:.1f} mm')

    outdir = Path(f'{basename}.pretty')
    outdir.mkdir(exist_ok=True)

    for target_length in targets.split(','):
        target_length = parse_length(target_length, default_unit='mm')

        if axis == 'width':
            scaling_factor = target_length / doc_w_mm
        else:
            scaling_factor = target_length / doc_h_mm

        instance_name = f'{basename}_{target_length:.1f}mm'
        outfile = outdir / f'{instance_name}.kicad_mod'
        print(f'{outfile}: Scaling to target {axis} {target_length:.1f} mm using scaling factor {scaling_factor:.3f}')

        try:
            proc = subprocess.run([svg_flatten,
                     '-o', 'sexp',
                     '--sexp-layer', sexp_layer,
                     '--sexp-mod-name', instance_name,
                     '--scale', str(scaling_factor),
                     input_svg], check=True, capture_output=True)
            outfile.write_bytes(proc.stdout)
        except FileNotFoundError:
            raise click.ClickException('Cannot find svg-flatten binary in PATH. You can give a custom path to the svg-flatten binary by setting the SVG_FLATTEN environment variable.')


if __name__ == '__main__':
    export()
