import tempfile
import logging
import os.path as path
from pathlib import Path
import shlex
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
from pathlib import Path

from bs4 import BeautifulSoup
import numpy as np
import click

import gerbonara as gn

__version__ = '3.1.8'

@click.group()
def cli():
    pass

@cli.command()
@click.argument('input_gerbers', type=click.Path(exists=True, path_type=Path))
@click.argument('input_svg', type=click.Path(exists=True, dir_okay=False, file_okay=True, allow_dash=True, path_type=Path))
@click.argument('output_gerbers', type=click.Path(allow_dash=True, path_type=Path))
@click.option('--dilate', default=0.1, type=float, help='Default dilation for subtraction operations in mm')
@click.option('--zip/--no-zip', 'is_zip', default=None, help='zip output files. Default: zip if output path ends with ".zip" or when outputting to stdout.')
@click.option('--curve-tolerance', type=float, help='Tolerance for curve flattening in mm')
@click.option('--no-subtract', 'no_subtract', flag_value=True, help='Disable subtraction')
@click.option('--subtract', help='Use user subtraction script from argument')
@click.option('--trace-space', type=float, default=0.1, help='passed through to svg-flatten')
@click.option('--vectorizer', help='passed through to svg-flatten')
@click.option('--vectorizer-map', help='passed through to svg-flatten')
@click.option('--excellon-conversion-errors', type=click.Choice(['raise', 'warn', 'ignore']), default='raise', help='Method of error handling during SVG to Excellon conversion')
@click.option('--preserve-aspect-ratio', help='PNG/JPG files only: passed through to svg-flatten')
@click.option('--exclude-groups', help='passed through to svg-flatten')
@click.option('--circle-test-tolerance', help='passed through to svg-flatten')
@click.option('--log-level', default='info', type=click.Choice(['debug', 'info', 'warning', 'error', 'critical']), help='log level')
def paste(input_gerbers, input_svg, output_gerbers, is_zip,
        dilate, curve_tolerance, no_subtract, subtract,
        preserve_aspect_ratio, circle_test_tolerance,
        trace_space, vectorizer, vectorizer_map, exclude_groups,
        excellon_conversion_errors, log_level):
    """ Render vector data and raster images from SVG file into gerbers. """

    logging.basicConfig(level=getattr(logging, log_level.upper()))

    subtract_map = parse_subtract_script('' if no_subtract else subtract, dilate)

    stack = gn.LayerStack.open(input_gerbers, lazy=True)
    (bb_min_x, bb_min_y), (bb_max_x, bb_max_y) = bounds = stack.board_bounds()

    output_is_zip = output_gerbers.name.lower().endswith('.zip') if is_zip is None else is_zip

    # Create output dir if it does not exist yet. Do this now so we fail early
    if not output_is_zip:
        output_gerbers.mkdir(exist_ok=True)

    @functools.lru_cache()
    def do_dilate(layer, amount):
        return dilate_gerber(layer, bounds, amount, curve_tolerance)

    with tempfile.NamedTemporaryFile(suffix='.svg') as processed_svg:
        run_cargo_command('usvg', *shlex.split(os.environ.get('USVG_OPTIONS', '')), input_svg, processed_svg.name)

        with open(processed_svg.name) as f:
            soup = BeautifulSoup(f.read(), features='xml')
    
        for (side, use), layer in [
                *stack.graphic_layers.items(),
                (('drill', 'plated'), stack.drill_pth),
                (('drill', 'nonplated'), stack.drill_npth)]:
            logging.info(f'Layer {side} {use}')
            if (soup_layer := soup.find('g', id=f'g-{side}-{use}')):
                if not soup_layer.contents:
                    logging.info(f'    Corresponding overlay layer is empty. Skipping.')
            else:
                logging.info(f'    Corresponding overlay layer not found. Skipping.')
                continue

            if layer is None:
                loggin.error(f'    Corresponding overlay layer is non-empty, but the corresponding layer could not be found in the input gerbers. Skipping.')
                continue
            
            # only open lazily loaded layer if we need it. Replace lazy wrapper in stack with loaded layer.
            layer = layer.instance
            logging.info(f'    Loaded layer: {layer}')

            overlay_grb = svg_to_gerber(processed_svg.name, no_usvg=True,
                    trace_space=trace_space, vectorizer=vectorizer, vectorizer_map=vectorizer_map,
                    exclude_groups=exclude_groups, curve_tolerance=curve_tolerance,
                    preserve_aspect_ratio=preserve_aspect_ratio, circle_test_tolerance=circle_test_tolerance,
                    outline_mode=(use == 'outline' or side == 'drill'),
                    only_groups=f'g-{side}-{use}')

            logging.info(f'    Converted overlay: {overlay_grb}')

            # move overlay from svg origin to gerber origin
            overlay_grb.offset(bb_min_x, bb_min_y)

            # dilated subtract layers on top of overlay
            if side in ('top', 'bottom'): # do not process subtraction scripts for inner layers, outline, and drill files
                dilations = subtract_map.get(use, [])
                for d_layer, amount in dilations:
                    dilated = do_dilate(stack[(side, d_layer)], amount)
                    layer.merge(dilated, mode='below', keep_settings=True)

            if side == 'drill':
                try:
                    overlay_grb = overlay_grb.to_excellon(plated=layer.is_plated_tristate,
                                                          errors=excellon_conversion_errors)
                except ValueError as e:
                    raise click.ClickException(f'Some objects on the {use} drill layer could not be converted from SVG to Excellon. This may be because they are not sufficiently circular to be matched. You can either increase the --circle-test-tolerance parameter from its default value of 0.1, or you can convert this error into a warning by passing --excellon-conversion-errors "warn" or "ignore".') from e

            # overlay on bottom
            layer.merge(overlay_grb, mode='below', keep_settings=True)
            logging.info(f'    Merged layer and overlay: {layer}')

        if output_is_zip:
            stack.save_to_zipfile(output_gerbers)
        else:
            stack.save_to_directory(output_gerbers)

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
    svg = stack.to_pretty_svg(side=('top' if top else 'bottom'), inkscape=True)

    template_layers = [f'{ttype}-copper', f'{ttype}-mask', f'{ttype}-silk', f'{ttype}-paste',
                       'mechanical outline', 'drill plated', 'drill nonplated']
    silk = template_layers[-2]

    if vector:
        output_svg.write_text(create_template_from_svg(svg, template_layers, current_layer=silk))

    else:
        with tempfile.NamedTemporaryFile(suffix='.svg') as temp_svg, \
            tempfile.NamedTemporaryFile(suffix='.png') as temp_png:
            Path(temp_svg.name).write_text(str(svg))
            run_cargo_command('resvg', temp_svg.name, temp_png.name, dpi=f'{raster_dpi:.0f}')
            output_svg.write_text(template_svg_for_png(stack.board_bounds(), Path(temp_png.name).read_bytes(),
                template_layers, current_layer=silk))


class ClickSizeParam(click.ParamType):
    name = 'Size'

    def convert(self, value, param, ctx):
        if isinstance(value, tuple):
            return value

        if not (m := re.match(r'([0-9]+\.?[0-9]*)(mm|cm|in)?[xX*/,×]([0-9]+\.?[0-9]*)(mm|cm|in)?', value)):
            self.fail('Size must have format [width]x[height][unit]. The unit can be mm, cm or in. The unit is optional and defaults to mm.', param=param, ctx=ctx)

        w, unit1, h, unit2 = m.groups()
        if unit1 and unit2 and unit1 != unit2:
            self.fail('Width and height must use the same unit. Two different units given for width and height: width is in {unit1}, and height is in {unit2}.', param=param, ctx=ctx)

        unit = (unit1 or unit2) or 'mm'
        return float(w), float(h), unit

@cli.command()
@click.argument('output_svg', type=click.Path(dir_okay=False, writable=True, allow_dash=True))
@click.option('--size', type=ClickSizeParam(), default='100x100mm', help='PCB size in [width]x[height][unit] format. Units can be cm, mm or in, when no unit is given, defaults to mm. When no size is given, defaults to 100x100mm.')
@click.option('--force', is_flag=True, help='Overwrite output file without asking if it exists.')
@click.option('-n', '--copper-layers', default=2, type=int, help='Number of copper layers to generate.')
@click.option('--no-default-layers', is_flag=True, help='Do not generate default layers.')
@click.option('-l', '--layer', multiple=True, help='Add given layer to the top of the output layer stack. Can be given multiple times.')
def empty_template(output_svg, size, force, copper_layers, no_default_layers, layer):
    if output_svg == '-':
        out = sys.stdout
    else:
        out = Path(output_svg)
        if out.exists():
            if not force and not click.confirm(f'Output file "{out}" already exists. Do you want to overwrite it?'):
                raise click.ClickException(f'Output file "{out}" already exists, exiting.')
        out = out.open('w')

    layers = layer or []
    current_layer = None
    if not no_default_layers:
        layers += ['top paste', 'top silk', 'top mask']

        if copper_layers > 0:
            current_layer = 'top copper'
            inner = [ 'inner{i} copper' for i in range(max(0, copper_layers-2)) ]
            layers += ['top copper', *inner, 'bottom copper'][:copper_layers]

        layers += ['bottom mask', 'bottom silk', 'bottom paste']
        layers += ['mechanical outline', 'drill plated', 'drill nonplated', 'other comments']
    if layers and current_layer is None:
        current_layer = layers[0]

    out.write(empty_pcb_template(size, layers, current_layer))
    out.flush()
    if output_svg != '-':
        out.close()


@cli.command()
@click.argument('input_svg', type=click.Path(exists=True, path_type=Path))
@click.argument('output_gerbers', type=click.Path(path_type=Path))
@click.option('-n', '--naming-scheme', default='kicad', type=click.Choice(['kicad', 'altium']), help='Naming scheme for gerber output file names.')
@click.option('--zip/--no-zip', 'is_zip', default=None, help='zip output files. Default: zip if output path ends with ".zip" or when outputting to stdout.')
@click.option('--composite-drill-file/--separate-drill-file', 'composite_drill', help='Use Altium composite Excellon drill file format (default)')
@click.option('--dilate', default=0.1, type=float, help='Default dilation for subtraction operations in mm')
@click.option('--curve-tolerance', type=float, help='Tolerance for curve flattening in mm')
@click.option('--subtract', help='Use user subtraction script from argument (default for "convert": none)')
@click.option('--trace-space', type=float, default=0.1, help='passed through to svg-flatten')
@click.option('--vectorizer', help='passed through to svg-flatten')
@click.option('--vectorizer-map', help='passed through to svg-flatten')
@click.option('--exclude-groups', help='passed through to svg-flatten')
@click.option('--circle-test-tolerance', help='passed through to svg-flatten')
@click.option('--pattern-complete-tiles-only', is_flag=True, help='passed through to svg-flatten')
@click.option('--use-apertures-for-patterns', is_flag=True, help='passed through to svg-flatten')
@click.option('--log-level', default='info', type=click.Choice(['debug', 'info', 'warning', 'error', 'critical']), help='log level')
def convert(input_svg, output_gerbers, is_zip, dilate, curve_tolerance, subtract, trace_space, vectorizer,
        vectorizer_map, exclude_groups, composite_drill, naming_scheme, circle_test_tolerance,
        pattern_complete_tiles_only, use_apertures_for_patterns, log_level):
    ''' Convert SVG file directly to gerbers.

    Unlike `gerbolyze paste`, this does not add the SVG's contents to existing gerbers. It allows you to directly create
    PCBs using Inkscape similar to PCBModE.
    '''
    logging.basicConfig(level=getattr(logging, log_level.upper()))

    subtract_map = parse_subtract_script(subtract, dilate, default_script='')
    output_is_zip = output_gerbers.name.lower().endswith('.zip') if is_zip is None else is_zip

    with tempfile.NamedTemporaryFile(suffix='.svg') as processed_svg:
        run_cargo_command('usvg', *shlex.split(os.environ.get('USVG_OPTIONS', '')), input_svg, processed_svg.name)

        soup = BeautifulSoup(input_svg.read_text(), features='xml')
        layers = {e.get('id'): e.get('inkscape:label') for e in soup.find_all('g', recursive=True)}

        stack = gn.LayerStack({}, None, None, [], board_name=input_svg.stem, original_path=input_svg)

        for group_id, label in layers.items():
            label = label or ''
            if not group_id or 'no export' in label:
                continue

            if not group_id.startswith('g-'):
                continue
            group_id = group_id[2:]

            if group_id == 'outline':
                side, use = 'mechanical', 'outline'
            elif group_id == 'comments':
                side, use = 'other', 'comments'
            elif len(group_id.split('-')) != 2:
                warnings.warn(f'Unknown layer {group_id}')
                continue
            else:
                side, use = group_id.split('-')

            grb = svg_to_gerber(processed_svg.name, no_usvg=True,
                    trace_space=trace_space, vectorizer=vectorizer, vectorizer_map=vectorizer_map,
                    exclude_groups=exclude_groups, curve_tolerance=curve_tolerance, only_groups=f'g-{group_id}',
                    circle_test_tolerance=circle_test_tolerance, pattern_complete_tiles_only=pattern_complete_tiles_only,
                    use_apertures_for_patterns=(use_apertures_for_patterns and use not in ('outline', 'drill')),
                    outline_mode=(use == 'outline' or side == 'drill'))
            grb.original_path = Path()

            if side == 'drill':
                if use == 'plated':
                    stack.drill_pth = grb.to_excellon(plated=True)
                elif use == 'nonplated':
                    stack.drill_npth = grb.to_excellon(plated=False)
                else:
                    warnings.warn(f'Invalid drill layer type "{side}". Must be one of "plated" or "nonplated"')

            else:
                stack.graphic_layers[(side, use)] = grb

        bounds = stack.board_bounds()
        @functools.lru_cache()
        def do_dilate(layer, amount):
            return dilate_gerber(layer, bounds, amount, curve_tolerance)

        for (side, use), layer in stack.graphic_layers.items():
            # dilated subtract layers on top of overlay
            if side in ('top', 'bottom'): # do not process subtraction scripts for inner layers
                dilations = subtract_map.get(use, [])
                for d_layer, amount in dilations:
                    d_layer =  stack.graphic_layers[(side, d_layer)]
                    dilated = do_dilate(d_layer, amount)
                    layer.merge(dilated, mode='above', keep_settings=True)

        if composite_drill:
            logging.info('Merging drill layers...')
            stack.merge_drill_layers()

        naming_scheme = getattr(gn.layers.NamingScheme, naming_scheme)
        if output_is_zip:
            stack.save_to_zipfile(output_gerbers, naming_scheme=naming_scheme)
        else:
            stack.save_to_directory(output_gerbers, naming_scheme=naming_scheme)


# Subtraction script handling
#============================

DEFAULT_SUB_SCRIPT = '''
out.silk -= in.mask
out.silk -= in.silk+0.5
out.mask -= in.mask+0.5
out.copper -= in.copper+0.5
'''

DEFAULT_CONVERT_SUB_SCRIPT = '''
out.silk -= in.mask
'''

def parse_subtract_script(script, default_dilation=0.1, default_script=DEFAULT_SUB_SCRIPT):
    if script is None:
        script = default_script

    subtract_script = {}
    lines = script.replace(';', '\n').splitlines()
    for line in lines:
        line = line.strip()
        if not line or line.startswith('#'):
            continue

        line = line.lower()
        line = re.sub(r'\s', '', line)

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

# Utility foo
# ===========

def run_cargo_command(binary, *args, **kwargs):
    cmd_args = []
    for key, value in kwargs.items():
        if value is not None:
            if value is False:
                continue

            cmd_args.append(f'--{key.replace("_", "-")}')

            if value is not True:
                cmd_args.append(value)
    cmd_args.extend(map(str, args))

    # By default, try a number of options:
    candidates = [
        # somewhere in $PATH
        binary,
        # wasi-wrapper in $PATH
        f'wasi-{binary}',
        # in user-local cargo installation
        Path.home() / '.cargo' / 'bin' / binary,
        # wasi-wrapper in user-local pip installation
        Path.home() / '.local' / 'bin' / f'wasi-{binary}',
        # next to our current python interpreter (e.g. in virtualenv)
        str(Path(sys.executable).parent / f'wasi-{binary}')
        ]

    # if envvar is set, try that first.
    if (env_var := os.environ.get(binary.upper())):
        candidates = [env_var, *candidates]

    for cand in candidates:
        try:
            logging.debug(f'trying {binary}: {cand}')
            logging.debug(f'with args: {" ".join(cmd_args)}')
            res = subprocess.run([cand, *cmd_args], check=True)
            break
        except FileNotFoundError:
            continue
    else:
        raise SystemError(f'{binary} executable not found')



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
    return f'<g id="g-{name.lower().replace(" ", "-")}" inkscape:label="{name}" inkscape:groupmode="layer"></g>'

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
          <sodipodi:namedview inkscape:current-layer="g-{current_layer.lower().replace(" ", "-")}" />
          <g inkscape:label="Preview" inkscape:groupmode="layer" id="g-preview" sodipodi:insensitive="true" style="opacity:0.5">
            <image x="0" y="0" width="{w_mm}" height="{h_mm}"
               xlink:href="data:image/jpeg;base64,{base64.b64encode(png_data).decode()}" />
          </g>
          {extra_layers}
        </svg>
        '''
    return textwrap.dedent(template)

def empty_pcb_template(size, extra_layers, current_layer):
    w, h, unit = size

    extra_layers = "\n  ".join(template_layer(name) for name in extra_layers)
    current_layer = f'<sodipodi:namedview inkscape:current-layer="g-{current_layer.lower().replace(" ", "-")}" />' if current_layer else ''

    # we set up the viewport such that document dimensions = document units = [unit]
    template = f'''<?xml version="1.0" encoding="UTF-8" standalone="no"?>
        <svg version="1.1"
           xmlns="http://www.w3.org/2000/svg"
           xmlns:xlink="http://www.w3.org/1999/xlink"
           xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
           xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
           width="{w}{unit}" height="{h}{unit}" viewBox="0 0 {w} {h}" >
          <defs/>
          {current_layer}
          {extra_layers}
        </svg>
        '''
    return textwrap.dedent(template)


MM_PER_INCH = 25.4

def create_template_from_svg(svg, extra_layers, current_layer):
    view, *layers = svg.children
    view.attrs['inkscape__current_layer'] = f'g-{current_layer.lower().replace(" ", "-")}'

    extra_layers = [ template_layer(name) for name in extra_layers ]
    svg.children = [ view, *extra_layers, gn.utils.Tag('g', layers, inkscape__label='Preview', sodipodi__insensitive='true',
        inkscape__groupmode='layer', style='opacity:0.5') ]

    return str(svg)

# SVG/gerber import
#==================

def dilate_gerber(layer, bounds, dilation, curve_tolerance):
    with tempfile.NamedTemporaryFile(suffix='.svg') as temp_in_svg,\
         tempfile.NamedTemporaryFile(suffix='.svg') as temp_out_svg:
        Path(temp_in_svg.name).write_text(str(layer.instance.to_svg(force_bounds=bounds, fg='white')))
        run_cargo_command('usvg', temp_in_svg.name, temp_out_svg.name)

        # dilate & render back to gerber
        # NOTE: Maybe reconsider or nicely document dilation semantics ; It is weird that negative dilations affect
        # clear color and positive affects dark colors
        out = svg_to_gerber(temp_out_svg.name, no_usvg=True, dilate=-dilation, curve_tolerance=curve_tolerance)
        return out

def svg_to_gerber(infile, outline_mode=False, **kwargs):
    infile = Path(infile)

    args = [ '--format', ('gerber-outline' if outline_mode else 'gerber'),
            '--precision', '6', # intermediate file, use higher than necessary precision
            ]
    
    for k, v in kwargs.items():
        if v:
            args.append('--' + k.replace('_', '-'))
            if not isinstance(v, bool):
                args.append(str(v))

    with tempfile.NamedTemporaryFile(suffix='.gbr') as temp_gbr:
        args += [str(infile), str(temp_gbr.name)]

        logging.debug(f'svg-flatten args: {" ".join(args)}')

        if 'SVG_FLATTEN' in os.environ:
            logging.debug('using svg-flatten at $SVG_FLATTEN')
            subprocess.run([os.environ['SVG_FLATTEN'], *args], check=True)

        else:
            # By default, try four options:
            for candidate in [
                    # somewhere in $PATH
                    'svg-flatten',
                    None, # direct WASI import
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
                    if candidate is None:
                        import svg_flatten_wasi
                        svg_flatten_wasi.run_svg_flatten.callback(args[-2], args[-1], args[:-2], no_usvg=False)
                        logging.debug('using svg_flatten_wasi python package') 

                    else:
                        subprocess.run([candidate, *args], check=True)
                        logging.debug('using svg-flatten at', candidate)

                    break
                except (FileNotFoundError, ModuleNotFoundError):
                    continue

            else:
                raise SystemError('svg-flatten executable not found')

        return gn.rs274x.GerberFile.open(temp_gbr.name)

if __name__ == '__main__':
    cli()
