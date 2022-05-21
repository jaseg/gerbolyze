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
@click.argument('input_gerbers')
@click.argument('output_gerbers')
@click.option('-t', '--top', help='Top side SVG or PNG overlay') 
@click.option('-b', '--bottom', help='Bottom side SVG or PNG overlay') 
@click.option('-o', '--outline', help='SVG file to use for board outline. Can be the same one used for --top or --bottom.') 
@click.option('--layer-top', help='Top side SVG or PNG target layer. Default: Map SVG layers to Gerber layers, map PNG to Silk.') 
@click.option('--layer-bottom', help='Bottom side SVG or PNG target layer. See --layer-top.')
@click.option('--bbox', help='Output file bounding box. Format: "w,h" to force [w] mm by [h] mm output canvas OR '
        '"x,y,w,h" to force [w] mm by [h] mm output canvas with its bottom left corner at the given input gerber '
        'coördinates. MUST MATCH --bbox GIVEN TO PREVIEW')
@click.option('--dilate', default=0.1, type=float, help='Default dilation for subtraction operations in mm')
@click.option('--curve-tolerance', type=float, help='Tolerance for curve flattening in mm')
@click.option('--no-subtract', 'no_subtract', flag_value=True, help='Disable subtraction')
@click.option('--subtract', help='Use user subtraction script from argument (see description above)')
@click.option('--trace-space', type=float, default=0.1, help='passed through to svg-flatten')
@click.option('--vectorizer', help='passed through to svg-flatten')
@click.option('--vectorizer-map', help='passed through to svg-flatten')
@click.option('--preserve-aspect-ratio', help='PNG/JPG files only: passed through to svg-flatten')
@click.option('--exclude-groups', help='passed through to svg-flatten')
def paste(input_gerbers, output_gerbers,
        top, bottom, outline, layer_top, layer_bottom,
        bbox,
        dilate, curve_tolerance, no_subtract, subtract,
        preserve_aspect_ratio,
        trace_space, vectorizer, vectorizer_map, exclude_groups):
    """ Render vector data and raster images from SVG file into gerbers. """

    if no_subtract:
        subtract_map = {}
    else:
        subtract_map = parse_subtract_script(subtract, dilate)

    if not top and not bottom:
        raise click.UsageError('Either --top or --bottom must be given')

    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir = Path(tmpdir)
        output_gerbers = Path(output_gerbers)
        input_gerbers = Path(input_gerbers)
        source = unpack_if_necessary(input_gerbers, tmpdir)
        matches = match_gerbers_in_dir(source)

        if input_gerbers.is_dir():
            # Create output dir if it does not exist yet
            output_gerbers.mkdir(exist_ok=True)

            # In case output dir already existed, remove files we will overwrite
            for in_file in source.iterdir():
                out_cand = output_gerbers / in_file.name
                out_cand.unlink(missing_ok=True)

        for side, in_svg_or_png, target_layer in [
                ('top', top, layer_top),
                ('bottom', bottom, layer_bottom),
                ('outline', outline, None)]:

            if not in_svg_or_png:
                continue

            if Path(in_svg_or_png).suffix.lower() in ['.png', '.jpg'] and target_layer is None:
                target_layer = 'silk'

            print()
            print('#########################################')
            print('processing ', side, 'input file ', in_svg_or_png)
            print('#########################################')
            print()

            if not matches[side]:
                warnings.warn(f'No input gerber files found for {side} side')
                continue

            try:
                units, layers = load_side(matches[side])
            except SystemError as e:
                raise click.UsageError(e.args)
            
            print('loaded layers:', list(layers.keys()))

            bounds = get_bounds(bbox, layers)
            print('bounds:', bounds)

            @functools.lru_cache()
            def do_dilate(layer, amount):
                print('dilating', layer, 'by', amount)
                outfile = tmpdir / f'dilated-{layer}-{amount}.gbr'
                dilate_gerber(layers, layer, amount, bbox, tmpdir, outfile, units, curve_tolerance)
                gbr = gerberex.read(str(outfile))
                gbr.offset(bounds[0][0], bounds[1][0])
                return gbr
            
            for layer, input_files in layers.items():
                if layer == 'drill':
                    continue

                if target_layer is not None:
                    if layer != target_layer:
                        continue

                (in_grb_path, in_grb), = input_files

                print()
                print('-----------------------------------------')
                print('processing side', side, 'layer', layer)
                print('-----------------------------------------')
                print()
                print('rendering layer', layer)
                overlay_file = tmpdir / f'overlay-{side}-{layer}.gbr'
                layer_arg = layer if target_layer is None else None # slightly confusing but trust me :)
                svg_to_gerber(in_svg_or_png, overlay_file,
                        trace_space, vectorizer, vectorizer_map, exclude_groups, curve_tolerance,
                        layer_bounds=bounds, preserve_aspect_ratio=preserve_aspect_ratio,
                        only_groups=f'g-{layer_arg.lower()}',
                        outline_mode=(layer == 'outline'))

                overlay_grb = gerberex.read(str(overlay_file))
                if not overlay_grb.primitives:
                    print(f'Overlay layer {layer} does not contain anything. Skipping.', file=sys.stderr)
                    continue

                print('compositing')
                comp = gerberex.GerberComposition()
                # overlay on bottom
                overlay_grb.offset(bounds[0][0], bounds[1][0])
                comp.merge(overlay_grb)
                # dilated subtract layers on top of overlay
                dilations = subtract_map.get(layer, [])
                for d_layer, amount in dilations:
                    print('processing dilation', d_layer, amount)
                    dilated = do_dilate(d_layer, amount)
                    comp.merge(dilated)
                # input on top of everything
                comp.merge(gerberex.rs274x.GerberFile.from_gerber_file(in_grb.cam_source))

                if input_gerbers.is_dir():
                    this_out = output_gerbers / in_grb_path.name
                else:
                    this_out = output_gerbers
                print('dumping to', this_out)
                comp.dump(this_out)
    
        if input_gerbers.is_dir():
            for in_file in source.iterdir():
                out_cand = output_gerbers / in_file.name
                if not out_cand.is_file():
                    print(f'Input file {in_file.name} remained unprocessed. Copying.', file=sys.stderr)
                    shutil.copy(in_file, out_cand)

@cli.command()
@click.argument('input', type=click.Path(exists=True))
@click.argument('output', required=False)
@click.option('-t' ,'--top', help='Render board top side.', is_flag=True)
@click.option('-b' ,'--bottom', help='Render board bottom side.', is_flag=True)
@click.option('-f' ,'--force', help='Overwrite existing output file when autogenerating file name.', is_flag=True)
@click.option('--vector/--raster', help='Embed preview renders into output file as SVG vector graphics instead of rendering them to PNG bitmaps. The resulting preview may slow down your SVG editor.')
@click.option('--raster-dpi', type=float, default=300.0, help='DPI for rastering preview')
@click.option('--bbox', help='Output file bounding box. Format: "w,h" to force [w] mm by [h] mm output canvas OR '
        '"x,y,w,h" to force [w] mm by [h] mm output canvas with its bottom left corner at the given input gerber '
        'coördinates.')
def template(input, output, top, bottom, force, bbox, vector, raster_dpi):
    ''' Generate SVG template for gerbolyze paste from gerber files.

    INPUT may be a gerber file, directory of gerber files or zip file with gerber files
    '''
    source = Path(input)

    if (bool(top) + bool(bottom))  != 1:
        raise click.UsageError('Excactly one of --top or --bottom must be given.')

    if output is None:
        # autogenerate output file name if none is given:
        # /path/to/gerber/dir -> /path/to/gerber/dir.preview-{top|bottom}.svg
        # /path/to/gerbers.zip -> /path/to/gerbers.zip.preview-{top|bottom}.svg
        # /path/to/single/file.grb -> /path/to/single/file.grb.preview-{top|bottom}.svg
    
        ttype = 'top' if top else 'bottom'
        output = source.parent / f'{source.name}.template-{ttype}.svg'
        click.echo(f'Writing output to {output}')

        if output.exists() and not force:
            raise UsageError(f'Autogenerated output file already exists. Please remote first, or use --force, or '
                    'explicitly give an output path.')

    else:
        output = Path(output)

    stack = gn.LayerStack.open(source, lazy=True)
    svg = str(stack.to_pretty_svg(side=('top' if top else 'bottom')))
    bounds = stack.outline.instance.bounding_box(default=((0, 0), (0, 0))) # returns MM by default

    if vector:
            output.write_text(create_template_from_svg(bounds, svg)) # All gerbonara SVG is in MM by default

    else:
        with tempfile.NamedTemporaryFile(suffix='.svg') as temp_svg, \
            tempfile.NamedTemporaryFile(suffix='.png') as temp_png:
            Path(temp_svg.name).write_text(svg)
            run_resvg(temp_svg.name, temp_png.name, dpi=f'{raster_dpi:.0f}')
            output.write_text(template_svg_for_png(bounds, Path(temp_png.name).read_bytes()))


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

DEFAULT_EXTRA_LAYERS = [ 'copper', 'mask', 'silk' ]

def template_layer(name):
    return f'<g id="g-{name.lower()}" inkscape:label="{name}" inkscape:groupmode="layer"></g>'

def template_svg_for_png(bounds, png_data, extra_layers=DEFAULT_EXTRA_LAYERS, current_layer='silk'):
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

def create_template_from_svg(bounds, svg_data, extra_layers=DEFAULT_EXTRA_LAYERS, current_layer='silk'):
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

def dilate_gerber(layers, layer_name, dilation, bbox, tmpdir, outfile, units, curve_tolerance):
    if layer_name not in layers:
        raise ValueError(f'Cannot dilate layer {layer_name}: layer not found in input dir')

    bounds = get_bounds(bbox, layers)
    (x_min_mm, x_max_mm), (y_min_mm, y_max_mm) = bounds

    origin_x = x_min_mm / MM_PER_INCH
    origin_y = y_min_mm / MM_PER_INCH

    width = (x_max_mm - x_min_mm) / MM_PER_INCH
    height = (y_max_mm - y_min_mm) / MM_PER_INCH

    tmpfile = tmpdir / 'dilate-tmp.svg'
    path, _gbr = layers[layer_name][0]
    # NOTE: gerbv has an undocumented maximum length of 20 chars for the arguments to --origin and --window_inch
    cmd = ['gerbv', '-x', 'svg',
        '--border=0',
        f'--origin={origin_x:.6f}x{origin_y:.6f}', f'--window_inch={width:.6f}x{height:.6f}',
        '--foreground=#ffffff',
        '-o', str(tmpfile), str(path)]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # dilate & render back to gerber
    # TODO: the scale parameter is a hack. ideally we would fix svg-flatten to handle input units correctly.
    svg_to_gerber(tmpfile, outfile, dilate=-dilation*72.0/25.4, usvg_dpi=72, scale=25.4/72.0, curve_tolerance=curve_tolerance)

def svg_to_gerber(infile, outfile,
        layer_bounds=None, outline_mode=False,
        **kwargs):

    infile = Path(infile)

    args = [ '--format', ('gerber-outline' if outline_mode else 'gerber'),
            '--precision', '6', # intermediate file, use higher than necessary precision
            ]
    
    if kwargs.get('force_png') or (infile.suffix.lower() in ['.jpg', '.png'] and not kwargs.get('force_svg')):
        (min_x, max_x), (min_y, max_y) = layer_bounds
        kwargs['size'] = f'{max_x - min_x}x{max_y - min_y}'

    for k, v in kwargs.items():
        if v is not None:
            args.append('--' + k.replace('_', '-'))
            if not isinstance(v, bool):
                args.append(str(v))

    args += [str(infile), str(outfile)]

    if 'SVG_FLATTEN' in os.environ:
        subprocess.run([os.environ['SVG_FLATTEN'], *args], check=True)

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
                break
            except FileNotFoundError:
                continue

        else:
            raise SystemError('svg-flatten executable not found')
    

if __name__ == '__main__':
    cli()
