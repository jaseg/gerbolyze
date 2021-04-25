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
import gerber
from gerber.render.cairo_backend import GerberCairoContext
import gerberex
import gerberex.rs274x
import numpy as np
import click
from slugify import slugify

@click.group()
def cli():
    pass


@cli.command()
@click.option('-l', '--layer', type=click.Choice(['silk', 'mask', 'copper']), default='silk')
@click.option('-x', '--exact', flag_value=True)
@click.option('--trace-space', type=float, default=0.1, help='passed through to svg-flatten')
@click.argument('side', type=click.Choice(['top', 'bottom']))
@click.argument('source')
@click.argument('target')
@click.argument('image')
@click.pass_context
def vectorize(ctx, side, layer, exact, source, target, image, trace_space):
    """ Compatibility command for Gerbolyze version 1.

        This command is deprecated, please update your code to call "gerbolyze paste" instead.
    """
    ctx.invoke(paste, 
        input_gerbers=source,
        output_gerbers=target,
        **{side: image, f'layer_{side}': layer},
        no_subtract=exact,
        trace_space=trace_space,
        vectorizer='binary-contours',
        preserve_aspect_ratio='meet')

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
                svg_to_gerber(in_svg_or_png, overlay_file, layer_arg,
                        trace_space, vectorizer, vectorizer_map, exclude_groups, curve_tolerance,
                        bounds_for_png=bounds, preserve_aspect_ratio=preserve_aspect_ratio,
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
@click.argument('input')
@click.option('-t' ,'--top', help='Top layer output file.')
@click.option('-b' ,'--bottom', help='Bottom layer output file. --top or --bottom may be given at once. If neither is given, autogenerate filenames.')
@click.option('--vector/--raster', help='Embed preview renders into output file as SVG vector graphics instead of rendering them to PNG bitmaps. The resulting preview may slow down your SVG editor.')
@click.option('--raster-dpi', type=float, default=300.0, help='DPI for rastering preview')
@click.option('--bbox', help='Output file bounding box. Format: "w,h" to force [w] mm by [h] mm output canvas OR '
        '"x,y,w,h" to force [w] mm by [h] mm output canvas with its bottom left corner at the given input gerber '
        'coördinates.')
def template(input, top, bottom, bbox, vector, raster_dpi):
    ''' Generate SVG template for gerbolyze paste from gerber files.

    INPUT may be a gerber file, directory of gerber files or zip file with gerber files
    '''
    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir = Path(tmpdir)
        source = Path(input)

        if not top and not bottom: # autogenerate two file names if neither --top nor --bottom are given
            # /path/to/gerber/dir -> /path/to/gerber/dir.preview-{top|bottom}.svg
            # /path/to/gerbers.zip -> /path/to/gerbers.zip.preview-{top|bottom}.svg
            # /path/to/single/file.grb -> /path/to/single/file.grb.preview-{top|bottom}.svg
            outfiles = {
                    'top': source.parent / f'{source.name}.preview-top.svg',
                    'bottom': source.parent / f'{source.name}.preview-top.svg' }
        else:
            outfiles = {
                    'top': Path(top) if top else None,
                    'bottom': Path(bottom) if bottom else None }

        source = unpack_if_necessary(source, tmpdir)
        matches = match_gerbers_in_dir(source)

        for side in ('top', 'bottom'):
            if not outfiles[side]:
                continue

            if not matches[side]:
                warnings.warn(f'No input gerber files found for {side} side')
                continue

            try:
                units, layers = load_side(matches[side])
            except SystemError as e:
                raise click.UsageError(e.args)

            # cairo-svg uses a hardcoded dpi value of 72. pcb-tools does something weird, so we have to scale things
            # here.
            scale  = 1/25.4 if units == 'metric' else 1.0 # pcb-tools gerber scale

            scale *= CAIRO_SVG_HARDCODED_DPI
            if not vector: # adapt scale for png export
                scale *= raster_dpi / CAIRO_SVG_HARDCODED_DPI

            bounds = get_bounds(bbox, layers)
            ctx = GerberCairoContext(scale=scale)
            for layer_name in LAYER_RENDER_ORDER:
                for _path, to_render in layers.get(layer_name, ()):
                    ctx.render_layer(to_render, bounds=bounds)

            filetype = 'svg' if vector else 'png'
            tmp_render = tmpdir / f'intermediate-{side}.{filetype}'
            ctx.dump(str(tmp_render))

            if vector:
                with open(tmp_render, 'rb') as f:
                    svg_data = f.read()

                with open(outfiles[side], 'wb') as f:
                    f.write(create_template_from_svg(bounds, svg_data))

            else: # raster
                with open(tmp_render, 'rb') as f:
                    png_data = f.read()

                with open(outfiles[side], 'w') as f:
                    f.write(template_svg_for_png(bounds, png_data))

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

# Gerber file name extensions for Altium/Protel | KiCAD | Eagle
# Note that in case of KiCAD these extensions occassionally change without notice. If you discover that this list is not
# up to date, please know that it's not my fault and submit an issue or send me an email.
LAYER_SPEC = {
        'top': {
            'paste':    '.gtp|-F_Paste.gbr|-F.Paste.gbr|.pmc',
            'silk':     '.gto|-F_Silkscreen.gbr|-F_SilkS.gbr|-F.SilkS.gbr|.plc',
            'mask':     '.gts|-F_Mask.gbr|-F.Mask.gbr|.stc',
            'copper':   '.gtl|-F_Cu.gbr|-F.Cu.gbr|.cmp',
            'outline':  '.gko|.gm1|-Edge_Cuts.gbr|-Edge.Cuts.gbr|.gmb',
            'drill':    '.drl|.txt|-npth.drl',
        },
        'bottom': {
            'paste':    '.gbp|-B_Paste.gbr|-B.Paste.gbr|.pms',
            'silk':     '.gbo|-B_Silkscreen.gbr|-B_SilkS.gbr|-B.SilkS.gbr|.pls',
            'mask':     '.gbs|-B_Mask.gbr|-B.Mask.gbr|.sts',
            'copper':   '.gbl|-B_Cu.gbr|-B.Cu.gbr|.sol',
            'outline':  '.gko|.gm1|-Edge_Cuts.gbr|-Edge.Cuts.gbr|.gmb',
            'drill':    '.drl|.txt|-npth.drl',
        },
        'outline': {
            'outline':  '.gko|.gm1|-Edge_Cuts.gbr|-Edge.Cuts.gbr|.gmb',
            'drill':    '.drl|.txt|-npth.drl',
        }
    }

# Maps keys from LAYER_SPEC to pcb-tools layer classes (see pcb-tools'es gerber/layers.py)
LAYER_CLASSES = {
            'silk':     'topsilk',
            'mask':     'topmask',
            'paste':    'toppaste',
            'copper':   'top',
            'outline':  'outline',
            'drill':    'drill',
        }

LAYER_RENDER_ORDER = [ 'copper', 'mask', 'silk', 'paste', 'outline', 'drill' ]

def match_gerbers_in_dir(path):
    out = {}
    for side, layers in LAYER_SPEC.items():
        out[side] = {}
        for layer, match in layers.items():
            l = list(find_gerber_in_dir(path, match))
            if l:
                out[side][layer] = l
    return out

def find_gerber_in_dir(path, extensions):
    exts = extensions.split('|')
    for entry in path.iterdir():
        if not entry.is_file():
            continue

        if any(entry.name.lower().endswith(suffix.lower()) for suffix in exts):
            yield entry

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

def unpack_if_necessary(source, tmpdir, dirname='input'):
    """ Handle command-line input paths. If path points to a directory, return unchanged. If path points to a zip file,
    unpack to a directory inside tmpdir and return that. If path points to a file that is not a zip, copy that file into
    a subdir of tmpdir and return that subdir. """
    # If source is not a directory with gerber files (-> zip/single gerber), make it one
    if not source.is_dir():
        tmp_indir = tmpdir / dirname
        tmp_indir.mkdir()

        if source.suffix.lower() == '.zip' or is_zipfile(source):
            with ZipFile(source) as f:
                f.extractall(path=tmp_indir)

        else: # single input file
            shutil.copy(source, tmp_indir)

        return tmp_indir

    else:
        return source

def load_side(side_matches):
    """ Load all gerber files for one side returned by match_gerbers_in_dir. """
    def load(layer, path):
        print('loading', layer, 'layer from:', path)
        grb = gerber.load_layer(str(path))
        grb.layer_class = LAYER_CLASSES.get(layer, 'unknown')
        return grb

    layers = { layer: [ (path, load(layer, path)) for path in files ]
            for layer, files in side_matches.items() }

    for layer, elems in layers.items():
        if len(elems) > 1 and layer != 'drill':
            raise SystemError(f'Multiple files found for layer {layer}: {", ".join(str(x) for x in side_matches[layer]) }')

    unitses = set(layer.cam_source.units for items in layers.values() for _path, layer in items)
    if len(unitses) != 1:
        # FIXME: we should ideally be able to deal with this. We'll have to figure out a way to update a
        # GerberCairoContext's scale in between layers.
        raise SystemError('Input gerber files mix metric and imperial units. Please fix your export.')
    units, = unitses

    return units, layers

# SVG export
#===========

DEFAULT_EXTRA_LAYERS = [ layer for layer in LAYER_RENDER_ORDER if layer != "drill" ]

def template_layer(name):
    return f'<g id="g-{slugify(name)}" inkscape:label="{name}" inkscape:groupmode="layer"></g>'

def template_svg_for_png(bounds, png_data, extra_layers=DEFAULT_EXTRA_LAYERS):
    (x1, x2), (y1, y2) = bounds
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

def create_template_from_svg(bounds, svg_data, extra_layers=DEFAULT_EXTRA_LAYERS):
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

    # make original group an inkscape layer
    orig_g = svg.find(SVG_NS+'g')
    orig_g.set('id', 'g-preview')
    orig_g.set(INKSCAPE_NS+'label', 'Preview')
    orig_g.set(SODIPODI_NS+'insensitive', 'true') # lock group
    orig_g.set('style', 'opacity:0.5')

    # add layers
    for layer in extra_layers:
        new_g = etree.SubElement(svg, SVG_NS+'g')
        new_g.set('id', f'g-{slugify(layer)}')
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
    print('dilation cmd:', ' '.join(cmd))
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # dilate & render back to gerber
    # TODO: the scale parameter is a hack. ideally we would fix svg-flatten to handle input units correctly.
    svg_to_gerber(tmpfile, outfile, dilate=-dilation*72.0/25.4, dpi=72, scale=25.4/72.0, curve_tolerance=curve_tolerance)

def svg_to_gerber(infile, outfile,
        layer=None, trace_space:'mm'=0.1,
        vectorizer=None, vectorizer_map=None,
        exclude_groups=None,
        dilate=None, curve_tolerance=None,
        dpi=None, scale=None, bounds_for_png=None,
        preserve_aspect_ratio=None,
        force_png=False, force_svg=False,
        outline_mode=False):

    infile = Path(infile)

    if 'SVG_FLATTEN' in os.environ:
        candidates = [os.environ['SVG_FLATTEN']]

    else:
        # By default, try three options:
        candidates = [
                # somewhere in $PATH
                'svg-flatten',
                # in user-local pip installation
                Path.home() / '.local' / 'bin' / 'svg-flatten',
                # next to our current python interpreter (e.g. in virtualenv
                str(Path(sys.executable).parent / 'svg-flatten'),
                # next to this python source file in the development repo
                str(Path(__file__).parent.parent / 'svg-flatten' / 'build' / 'svg-flatten') ]

    args = [ '--format', ('gerber-outline' if outline_mode else 'gerber'),
            '--precision', '6', # intermediate file, use higher than necessary precision
            '--trace-space', str(trace_space) ]
    if layer:
        args += ['--only-groups', f'g-{slugify(layer)}']
    if vectorizer:
        args += ['--vectorizer', vectorizer]
    if vectorizer_map:
        args += ['--vectorizer-map', vectorizer_map]
    if exclude_groups:
        args += ['--exclude-groups', exclude_groups]
    if dilate:
        args += ['--dilate', str(dilate)]
    if curve_tolerance is not None:
        print('applying curve tolerance', curve_tolerance)
        args += ['--curve-tolerance', str(curve_tolerance)]
    if dpi:
        args += ['--usvg-dpi', str(dpi)]
    if scale:
        args += ['--scale', str(scale)]
    if force_png or (infile.suffix.lower() in ['.jpg', '.png'] and not force_svg):
        (min_x, max_x), (min_y, max_y) = bounds_for_png
        args += ['--size', f'{max_x - min_x}x{max_y - min_y}']
    if force_svg and force_png:
        raise ValueError('both force_svg and force_png given')
    if force_svg:
        args += ['--force-svg']
    if force_png:
        args += ['--force-png']
    if preserve_aspect_ratio:
        args += ['--preserve-aspect-ratio', preserve_aspect_ratio]

    args += [str(infile), str(outfile)]

    print('svg-flatten args:', ' '.join(args))
    for candidate in candidates:
        try:
            res = subprocess.run([candidate, *args], check=True)
            print('used svg-flatten:', candidate)
            break
        except FileNotFoundError:
            continue
    else:
        raise SystemError('svg-flatten executable not found')
    

if __name__ == '__main__':
    cli()
