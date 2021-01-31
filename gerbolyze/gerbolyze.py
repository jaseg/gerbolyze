import tempfile
import os.path as path
from pathlib import Path
import textwrap
import os
import base64
import re
import sys
import warnings
import time
import shutil
import math
from zipfile import ZipFile, is_zipfile
import shutil
from lxml import etree

import gerber
from gerber.render.cairo_backend import GerberCairoContext
import numpy as np
import cv2
import enum
import tqdm
import click
from slugify import slugify

@click.command()
@click.argument('input')
@click.option('-t' ,'--top', help='Top layer output file.')
@click.option('-b' ,'--bottom', help='Bottom layer output file. --top or --bottom may be given at once. If neither is given, autogenerate filenames.')
@click.option('--vector/--raster', help='Embed preview renders into output file as SVG vector graphics instead of rendering them to PNG bitmaps. The resulting preview may slow down your SVG editor.')
@click.option('--raster-dpi', type=float, default=300.0, help='DPI for rastering preview')
@click.option('--bbox', help='Output file bounding box. Format: "w,h" to force [w] mm by [h] mm output canvas OR '
        '"x,y,w,h" to force [w] mm by [h] mm output canvas with its bottom left corner at the given input gerber '
        'coördinates.')
def render_preview(input, top, bottom, bbox, vector, raster_dpi):
    ''' Render gerber file into template to be used with gerbolyze --vectorize.

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

        # If source is not a directory with gerber files (-> zip/single gerber), make it one
        if not source.is_dir():
            tmp_indir = tmpdir / 'input'
            tmp_indir.mkdir()

            if source.suffix.lower() == '.zip' or is_zipfile(source):
                with ZipFile(source) as f:
                    f.extractall(path=tmp_indir)

            else: # single input file
                shutil.copy(source, tmp_indir)

            source = tmp_indir
        # source now is a directory with gerber files.

        bounds = None
        if bbox:
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
            bounds = ((x, x+w), (y, y+h))

        matches = match_gerbers_in_dir(source)
        for side in ('top', 'bottom'):
            flattened =  [ e for for_this_layer in matches[side].values() for e in for_this_layer ]

            if not outfiles[side]:
                continue

            if not flattened:
                warnings.warn(f'No input gerber files found for {side} side')
                continue

            def load(layer, path):
                print('loading', layer, 'layer from:', path)
                grb = gerber.load_layer(str(path))
                grb.layer_class = LAYER_CLASSES.get(layer, 'unknown')
                return grb

            layers = { layer: [ load(layer, path) for path in files ]
                    for layer, files in matches[side].items()
                    if files }

            for layer, elems in layers.items():
                if len(elems) > 1 and layer != 'drill':
                    raise click.UsageError(f'Multiple files found for layer {layer}: {", ".join(matches[side][layer]) }')

            unitses = set(layer.cam_source.units for items in layers.values() for layer in items)
            if len(unitses) != 1:
                # FIXME: we should ideally be able to deal with this. We'll have to figure out a way to update a
                # GerberCairoContext's scale in between layers.
                raise SystemError('Input gerber files mix metric and imperial units. Please fix your export.')
            units, = unitses

            # cairo-svg uses a hardcoded dpi value of 72. pcb-tools does something weird, so we have to scale things
            # here.
            scale  = 1/25.4 if units == 'metric' else 1.0 # pcb-tools gerber scale

            # this is fixed, we cannot tell cairo-svg to use some other value. we just have to work around it.
            CAIRO_SVG_HARDCODED_DPI = 72.0

            scale *= CAIRO_SVG_HARDCODED_DPI # cairo-svg dpi
            if not vector: # adapt scale for png export
                scale *= raster_dpi / CAIRO_SVG_HARDCODED_DPI

            # NOTE: When the user has not set explicit bounds, we automatically extract the design's bounding box from
            # the input gerber files. If a folder is used as input, we use the outline gerber and barf if we can't find
            # one. If only a single file is given, we simply use that file's bounding box
            #
            # We have to do things this way since gerber files do not have explicit bounds listed.
            #
            # Note that the bounding box extracted from the outline layer usually will be one outline layer stroke widht
            # larger in all directions than the finished board. 
            if not bounds:
                if 'outline' in layers:
                    bounds = calculate_apertureless_bounding_box(layers['outline'][0].cam_source)

                elif len(flattened) == 1:
                    bounds = flattened[0].cam_source.bounding_box

                else:
                    raise click.UsageError('Cannot find an outline file and no --bbox given.')

            ctx = GerberCairoContext(scale=scale)
            for layer_name in LAYER_RENDER_ORDER:
                for to_render in layers.get(layer_name, ()):
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

# Utility foo
# ===========

# Gerber file name extensions for Altium/Protel | KiCAD | Eagle
# Note that in case of KiCAD these extensions occassionally change without notice. If you discover that this list is not
# up to date, please know that it's not my fault and submit an issue or send me an email.
LAYER_SPEC = {
        'top': {
            'paste':    '.gtp|-F_Paste.gbr|-F.Paste.gbr|.pmc',
            'silk':     '.gto|-F_SilkS.gbr|-F.SilkS.gbr|.plc',
            'mask':     '.gts|-F_Mask.gbr|-F.Mask.gbr|.stc',
            'copper':   '.gtl|-F_Cu.gbr|-F.Cu.gbr|.cmp',
            'outline':  '.gko|.gm1|-Edge_Cuts.gbr|-Edge.Cuts.gbr|.gmb',
            'drill':    '.drl|.txt|-npth.drl',
        },
        'bottom': {
            'paste':    '.gbp|-B_Paste.gbr|-B.Paste.gbr|.pms',
            'silk':     '.gbo|-B_SilkS.gbr|-B.SilkS.gbr|.pls',
            'mask':     '.gbs|-B_Mask.gbr|-B.Mask.gbr|.sts',
            'copper':   '.gbl|-B_Cu.gbr|-B.Cu.gbr|.sol',
            'outline':  '.gko|.gm1|-Edge_Cuts.gbr|-Edge.Cuts.gbr|.gmb',
            'drill':    '.drl|.txt|-npth.drl',
        },
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
            out[side][layer] = list(find_gerber_in_dir(path, match))
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

MM_PER_INCH = 25.4

def svg_pt_to_mm(pt_len, dpi=72.0):
    if pt_len.endswith('pt'):
        pt_len = pt_len[:-2]

    return f'{float(pt_len) / dpi * MM_PER_INCH}mm'

def create_template_from_svg(bounds, svg_data, extra_layers=DEFAULT_EXTRA_LAYERS):
    svg = etree.fromstring(svg_data)

    # add inkscape namespaces
    NS = '{http://www.w3.org/2000/svg}'
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
    orig_g = svg.find(NS+'g')
    orig_g.set('id', 'g-preview')
    orig_g.set(INKSCAPE_NS+'label', 'Preview')
    orig_g.set(SODIPODI_NS+'insensitive', 'true') # lock group
    orig_g.set('style', 'opacity:0.5')

    # add layers
    for layer in extra_layers:
        new_g = etree.SubElement(svg, NS+'g')
        new_g.set('id', f'g-{slugify(layer)}')
        new_g.set(INKSCAPE_NS+'label', layer)
        new_g.set(INKSCAPE_NS+'groupmode', 'layer')

    return etree.tostring(svg)

if __name__ == '__main__':
    render_preview()
