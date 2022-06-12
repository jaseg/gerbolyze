import tempfile
import os.path as path
from pathlib import Path
import textwrap
import subprocess
import functools
import os
import base64
import re
import sys
import warnings
import shutil
from zipfile import ZipFile, is_zipfile

from lxml import etree
import numpy as np
import click

import gerbonara as gn

@click.group()
def cli():
    pass

@cli.command()
@click.argument('input_gerbers', type=click.Path(exists=True))
@click.argument('input_svg', type=click.Path(exists=True, dir_okay=False, file_okay=True, allow_dash=True))
@click.argument('output_gerbers')
@click.option('--dilate', default=0.1, type=float, help='Default dilation for subtraction operations in mm')
@click.option('--curve-tolerance', type=float, help='Tolerance for curve flattening in mm')
@click.option('--no-subtract', 'no_subtract', flag_value=True, help='Disable subtraction')
@click.option('--subtract', help='Use user subtraction script from argument (see description above)')
@click.option('--trace-space', type=float, default=0.1, help='passed through to svg-flatten')
@click.option('--vectorizer', help='passed through to svg-flatten')
@click.option('--vectorizer-map', help='passed through to svg-flatten')
@click.option('--preserve-aspect-ratio', help='PNG/JPG files only: passed through to svg-flatten')
@click.option('--exclude-groups', help='passed through to svg-flatten')
def paste(input_gerbers, input_svg, output_gerbers,
        dilate, curve_tolerance, no_subtract, subtract,
        preserve_aspect_ratio,
        trace_space, vectorizer, vectorizer_map, exclude_groups):
    """ Render vector data and raster images from SVG file into gerbers. """

    if no_subtract:
        subtract_map = {}
    else:
        subtract_map = parse_subtract_script(subtract, dilate)

    output_gerbers = Path(output_gerbers)
    input_gerbers = Path(input_gerbers)
    stack = gn.LayerStack.open(input_gerbers, lazy=True)
    (bb_min_x, bb_min_y), (bb_max_x, bb_max_y) = bounds = stack.board_bounds()

    # Create output dir if it does not exist yet. Do this now so we fail early
    if input_gerbers.is_dir():
        output_gerbers.mkdir(exist_ok=True)

        # In case output dir already existed, remove files we will overwrite
        for in_file in input_gerbers.iterdir():
            out_cand = output_gerbers / in_file.name
            out_cand.unlink(missing_ok=True)

    else: # We are working on a zip file
        tempdir = tempfile.NamedTemporaryDirectory()

    @functools.lru_cache()
    def do_dilate(layer, amount):
        return dilate_gerber(layer, bounds, amount, curve_tolerance)
    
    for (side, use), layer in stack.graphic_layers.items():
        print('processing', side, use, 'layer')
        overlay_grb = svg_to_gerber(input_svg,
                trace_space=trace_space, vectorizer=vectorizer, vectorizer_map=vectorizer_map,
                exclude_groups=exclude_groups, curve_tolerance=curve_tolerance,
                preserve_aspect_ratio=preserve_aspect_ratio,
                only_groups=f'g-{side}-{use}')
        # FIXME outline mode, also process outline layer

        if not overlay_grb:
            print(f'Overlay {side} {use} layer is empty. Skipping.', file=sys.stderr)
            continue

        # only open lazily loaded layer if we need it. Replace lazy wrapper in stack with loaded layer.
        stack.graphic_layers[(side, use)] = layer = layer.instance

        # move overlay from svg origin to gerber origin
        overlay_grb.offset(bb_min_x, bb_min_y)

        print('compositing')
        # dilated subtract layers on top of overlay
        if side in ('top', 'bottom'): # do not process subtraction scripts for inner layers
            dilations = subtract_map.get(use, [])
            for d_layer, amount in dilations:
                print('processing dilation', d_layer, amount)
                dilated = do_dilate(stack[(side, d_layer)], amount)
                layer.merge(dilated, mode='below', keep_settings=True)

        # overlay on bottom
        layer.merge(overlay_grb, mode='below', keep_settings=True)

    if input_gerbers.is_dir():
        stack.save_to_directory(output_gerbers)
    else:
        stack.save_to_zipfile(output_gerbers)

@cli.command()
@click.argument('input_gerbers', type=click.Path(exists=True))
@click.argument('output_svg', required=False)
@click.option('-t' ,'--top', help='Render board top side.', is_flag=True)
@click.option('-b' ,'--bottom', help='Render board bottom side.', is_flag=True)
@click.option('-f' ,'--force', help='Overwrite existing output file when autogenerating file name.', is_flag=True)
@click.option('--vector/--raster', help='Embed preview renders into output file as SVG vector graphics instead of rendering them to PNG bitmaps. The resulting preview may slow down your SVG editor.')
@click.option('--raster-dpi', type=float, default=300.0, help='DPI for rastering preview')
def template(input_gerbers, output_svg, top, bottom, force, vector, raster_dpi):
    ''' Generate SVG template for gerbolyze paste from gerber files.

    INPUT may be a gerber file, directory of gerber files or zip file with gerber files
    '''
    source = Path(input_gerbers)
    ttype = 'top' if top else 'bottom'

    if (bool(top) + bool(bottom))  != 1:
        raise click.UsageError('Excactly one of --top or --bottom must be given.')

    if output_svg is None:
        # autogenerate output file name if none is given:
        # /path/to/gerber/dir -> /path/to/gerber/dir.preview-{top|bottom}.svg
        # /path/to/gerbers.zip -> /path/to/gerbers.zip.preview-{top|bottom}.svg
        # /path/to/single/file.grb -> /path/to/single/file.grb.preview-{top|bottom}.svg
    
        output_svg = source.parent / f'{source.name}.template-{ttype}.svg'
        click.echo(f'Writing output to {output_svg}')

        if output_svg.exists() and not force:
            raise UsageError(f'Autogenerated output file already exists. Please remote first, or use --force, or '
                    'explicitly give an output path.')

    else:
        output_svg = Path(output_svg)

    stack = gn.LayerStack.open(source, lazy=True)
    bounds = stack.board_bounds()
    svg = str(stack.to_pretty_svg(side=('top' if top else 'bottom'), force_bounds=bounds))

    template_layers = [ f'{ttype}-{use}' for use in [ 'copper', 'mask', 'silk' ] ]
    silk = template_layers[-1]

    if vector:
        # All gerbonara SVG is in MM by default
        output_svg.write_text(create_template_from_svg(bounds, svg, template_layers, current_layer=silk))

    else:
        with tempfile.NamedTemporaryFile(suffix='.svg') as temp_svg, \
            tempfile.NamedTemporaryFile(suffix='.png') as temp_png:
            Path(temp_svg.name).write_text(svg)
            run_resvg(temp_svg.name, temp_png.name, dpi=f'{raster_dpi:.0f}')
            output_svg.write_text(template_svg_for_png(bounds, Path(temp_png.name).read_bytes(),
                template_layers, current_layer=silk))


# Subtraction script handling
#============================

DEFAULT_SUB_SCRIPT = '''
out.silk -= in.mask
out.silk -= in.silk+0.5
out.mask -= in.mask+0.5
out.copper -= in.copper+0.5
'''

def parse_subtract_script(script, default_dilation=0.1):
    if script is None:
        script = DEFAULT_SUB_SCRIPT

    subtract_script = {}
    lines = script.replace(';', '\n').splitlines()
    for line in lines:
        line = line.strip()
        if not line or line.startswith('#'):
            continue

        line = line.lower()
        line = re.sub('\s', '', line)

        # out.copper -= in.copper+0.1
        varname = r'([a-z]+\.[a-z]+)'
        floatnum = r'([+-][.0-9]+)'
        match = re.fullmatch(fr'{varname}-={varname}{floatnum}?', line)
        if not match:
            raise ValueError(f'Cannot parse line: {line}')
        
        out_var, in_var, dilation = match.groups()
        if not out_var.startswith('out.') or not in_var.startswith('in.'):
            raise ValueError('All left-hand side values must be outputs, right-hand side values must be inputs.')

        _out, _, out_layer = out_var.partition('.')
        _in, _, in_layer = in_var.partition('.')

        dilation = float(dilation) if dilation else default_dilation

        subtract_script[out_layer] = subtract_script.get(out_layer, []) + [(in_layer, dilation)]
    return subtract_script

# Parameter parsing foo
#======================

def parse_bbox(bbox):
    if not bbox:
        return None
    elems = [ int(elem) for elem in re.split('[,/ ]', bbox) ]
    if len(elems) not in (2, 4):
        raise click.BadParameter(
                '--bbox must be either two floating-point values like: w,h or four like: x,y,w,h')

    elems = [ float(e) for e in elems ]

    if len(elems) == 2:
        bounds = [0, 0, *elems]
    else:
        bounds = elems
    
    # now transform bounds to the format pcb-tools uses. Instead of (x, y, w, h) or even (x1, y1, x2, y2), that
    # is ((x1, x2), (y1, y2)

    x, y, w, h = bounds
    return ((x, x+w), (y, y+h))

def bounds_from_outline(layers):
    ''' NOTE: When the user has not set explicit bounds, we automatically extract the design's bounding box from the
    input gerber files. If a folder is used as input, we use the outline gerber and barf if we can't find one. If only a
    single file is given, we simply use that file's bounding box
    
    We have to do things this way since gerber files do not have explicit bounds listed.
    
    Note that the bounding box extracted from the outline layer usually will be one outline layer stroke widht larger in
    all directions than the finished board. 
    '''
    if 'outline' in layers:
        outline_files = layers['outline']
        _path, grb = outline_files[0]
        return calculate_apertureless_bounding_box(grb.cam_source)

    elif len(layers) == 1:
        first_layer, *rest = layers.values()
        first_file, *rest = first_layer
        _path, grb = first_file
        return grb.cam_source.bounding_box

    else:
        raise click.UsageError('Cannot find an outline file and no --bbox given.')

def get_bounds(bbox, layers):
    bounds = parse_bbox(bbox)
    if bounds:
        return bounds
    return bounds_from_outline(layers)

# Utility foo
# ===========

def run_resvg(input_file, output_file, **resvg_args):

    args = []
    for key, value in resvg_args.items():
        if value is not None:
            if value is False:
                continue

            args.append(f'--{key.replace("_", "-")}')

            if value is not True:
                args.append(value)

    args += [input_file, output_file]

    # By default, try a number of options:
    candidates = [
        # somewhere in $PATH
        'resvg',
        'wasi-resvg',
        # in user-local cargo installation
        Path.home() / '.cargo' / 'bin' / 'resvg',
        # wasi-resvg in user-local pip installation
        Path.home() / '.local' / 'bin' / 'wasi-resvg',
        # next to our current python interpreter (e.g. in virtualenv)
        str(Path(sys.executable).parent / 'wasi-resvg')
        ]

    # if RESVG envvar is set, try that first.
    if 'RESVG' in os.environ:
        exec_candidates = [os.environ['RESVG'], *exec_candidates]

    for candidate in candidates:
        try:
            res = subprocess.run([candidate, *args], check=True)
            print('used resvg:', candidate)
            break
        except FileNotFoundError:
            continue
    else:
        raise SystemError('resvg executable not found')



def calculate_apertureless_bounding_box(cam):
    ''' pcb-tools'es default bounding box function returns the bounding box of the primitives including apertures (i.e.
    line widths). For determining a board's size from the outline layer, we want the bounding box disregarding
    apertures.
    '''

    min_x = min_y = 1000000
    max_x = max_y = -1000000

    for prim in cam.primitives:
        bounds = prim.bounding_box_no_aperture
        min_x = min(bounds[0][0], min_x)
        max_x = max(bounds[0][1], max_x)

        min_y = min(bounds[1][0], min_y)
        max_y = max(bounds[1][1], max_y)

    return ((min_x, max_x), (min_y, max_y))

# SVG export
#===========

def template_layer(name):
    return f'<g id="g-{name.lower()}" inkscape:label="{name}" inkscape:groupmode="layer"></g>'

def template_svg_for_png(bounds, png_data, extra_layers, current_layer):
    (x1, y1), (x2, y2) = bounds
    w_mm, h_mm = (x2 - x1), (y2 - y1)

    extra_layers = "\n  ".join(template_layer(name) for name in extra_layers)

    # we set up the viewport such that document dimensions = document units = mm
    template = f'''<?xml version="1.0" encoding="UTF-8" standalone="no"?>
        <svg version="1.1"
           xmlns="http://www.w3.org/2000/svg"
           xmlns:xlink="http://www.w3.org/1999/xlink"
           xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
           xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
           width="{w_mm}mm" height="{h_mm}mm" viewBox="0 0 {w_mm} {h_mm}" >
          <defs/>
          <sodipodi:namedview inkscape:current-layer="g-{current_layer.lower()}" />
          <g inkscape:label="Preview" inkscape:groupmode="layer" id="g-preview" sodipodi:insensitive="true" style="opacity:0.5">
            <image x="0" y="0" width="{w_mm}" height="{h_mm}"
               xlink:href="data:image/jpeg;base64,{base64.b64encode(png_data).decode()}" />
          </g>
          {extra_layers}
        </svg>
        '''
    return textwrap.dedent(template)

# this is fixed, we cannot tell cairo-svg to use some other value. we just have to work around it.
CAIRO_SVG_HARDCODED_DPI = 72.0
MM_PER_INCH = 25.4

def svg_pt_to_mm(pt_len, dpi=CAIRO_SVG_HARDCODED_DPI):
    if pt_len.endswith('pt'):
        pt_len = pt_len[:-2]

    return f'{float(pt_len) / dpi * MM_PER_INCH}mm'

def create_template_from_svg(bounds, svg_data, extra_layers):
    svg = etree.fromstring(svg_data)

    # add inkscape namespaces
    SVG_NS = '{http://www.w3.org/2000/svg}'
    INKSCAPE_NS = 'http://www.inkscape.org/namespaces/inkscape'
    SODIPODI_NS = 'http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd'
    # glObAL stAtE YaY
    etree.register_namespace('inkscape', INKSCAPE_NS)
    etree.register_namespace('sodipodi', SODIPODI_NS)
    INKSCAPE_NS = '{'+INKSCAPE_NS+'}'
    SODIPODI_NS = '{'+SODIPODI_NS+'}'

    # convert document units to mm
    svg.set('width', svg_pt_to_mm(svg.get('width')))
    svg.set('height', svg_pt_to_mm(svg.get('height')))

    # add inkscape <namedview> elem to set currently selected layer
    namedview_elem = etree.SubElement(svg, SODIPODI_NS+'namedview')
    namedview_elem.set('id', "namedview23")
    namedview_elem.set(INKSCAPE_NS+'current-layer', f'g-{current_layer}')

    # make original group an inkscape layer
    orig_g = svg.find(SVG_NS+'g')
    orig_g.set('id', 'g-preview')
    orig_g.set(INKSCAPE_NS+'label', 'Preview')
    orig_g.set(SODIPODI_NS+'insensitive', 'true') # lock group
    orig_g.set('style', 'opacity:0.5')

    # add layers
    for layer in extra_layers:
        new_g = etree.SubElement(svg, SVG_NS+'g')
        new_g.set('id', f'g-{layer.lower()}')
        new_g.set(INKSCAPE_NS+'label', layer)
        new_g.set(INKSCAPE_NS+'groupmode', 'layer')

    return etree.tostring(svg)

# SVG/gerber import
#==================

def dilate_gerber(layer, bounds, dilation, curve_tolerance):
    with tempfile.NamedTemporaryFile(suffix='.svg') as temp_svg:
        Path(temp_svg.name).write_text(str(layer.instance.to_svg(force_bounds=bounds, fg='white')))

        (bb_min_x, bb_min_y), (bb_max_x, bb_max_y) = bounds
        # dilate & render back to gerber
        # NOTE: Maybe reconsider or nicely document dilation semantics ; It is weird that negative dilations affect
        # clear color and positive affects dark colors
        out = svg_to_gerber(temp_svg.name, dilate=-dilation, curve_tolerance=curve_tolerance)
        return out

def svg_to_gerber(infile, outline_mode=False, **kwargs):
    infile = Path(infile)

    args = [ '--format', ('gerber-outline' if outline_mode else 'gerber'),
            '--precision', '6', # intermediate file, use higher than necessary precision
            ]
    
    for k, v in kwargs.items():
        if v is not None:
            args.append('--' + k.replace('_', '-'))
            if not isinstance(v, bool):
                args.append(str(v))

    with tempfile.NamedTemporaryFile(suffix='.gbr') as temp_gbr:
        args += [str(infile), str(temp_gbr.name)]

        if 'SVG_FLATTEN' in os.environ:
            print('svg-flatten args:', args)
            subprocess.run([os.environ['SVG_FLATTEN'], *args], check=True)
            print('used svg-flatten at $SVG_FLATTEN')

        else:
            # By default, try four options:
            for candidate in [
                    # somewhere in $PATH
                    'svg-flatten',
                    'wasi-svg-flatten',

                    # in user-local pip installation
                    Path.home() / '.local' / 'bin' / 'svg-flatten',
                    Path.home() / '.local' / 'bin' / 'wasi-svg-flatten',

                    # next to our current python interpreter (e.g. in virtualenv)
                    str(Path(sys.executable).parent / 'svg-flatten'),
                    str(Path(sys.executable).parent / 'wasi-svg-flatten'),

                    # next to this python source file in the development repo
                    str(Path(__file__).parent.parent / 'svg-flatten' / 'build' / 'svg-flatten') ]:

                try:
                    subprocess.run([candidate, *args], check=True)
                    print('used svg-flatten at', candidate)
                    break
                except FileNotFoundError:
                    continue

            else:
                raise SystemError('svg-flatten executable not found')

        return gn.rs274x.GerberFile.open(temp_gbr.name)
    

if __name__ == '__main__':
    cli()
