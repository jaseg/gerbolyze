#!/usr/bin/env python3

import subprocess
import zipfile
import tempfile
import os.path as path
import os
import sys
import time
import shutil
import math

import tqdm
import gerber
from gerber.render import GerberCairoContext
import numpy as np
import cv2
import enum

class Unit(enum.Enum):
    MM = 0
    INCH = 1
    MIL = 2


def generate_mask(
        outline,
        target,
        scale,
        debugimg,
        status_print,
        gerber_unit,
        extend_overlay_r_mil,
        subtract_gerber
        ):
    # Render all gerber layers whose features are to be excluded from the target image, such as board outline, the
    # original silk layer and the solder paste layer to binary images.
    with tempfile.TemporaryDirectory() as tmpdir:
        img_file = path.join(tmpdir, 'target.png')

        status_print('Combining keepout composite')
        fg, bg = gerber.render.RenderSettings((1, 1, 1)), gerber.render.RenderSettings((0, 0, 0))
        ctx = GerberCairoContext(scale=scale)
        status_print('  * outline')
        ctx.render_layer(outline, settings=fg, bgsettings=bg)
        status_print('  * target layer')
        ctx.render_layer(target, settings=fg, bgsettings=bg)
        for fn, sub in subtract_gerber:
            status_print('  * extra layer', os.path.basename(fn))
            layer = gerber.loads(sub)
            ctx.render_layer(layer, settings=fg, bgsettings=bg)
        status_print('Rendering keepout composite')
        ctx.dump(img_file)

        # Vertically flip exported image
        original_img = cv2.imread(img_file, cv2.IMREAD_GRAYSCALE)[::-1, :]

    f = 1 if gerber_unit == Unit.INCH else 25.4 # MM
    r = 1+2*max(1, int(extend_overlay_r_mil/1000 * f * scale))
    status_print('Expanding keepout composite by', r)

    # Extend image by a few pixels and flood-fill from (0, 0) to mask out the area outside the outermost outline
    # This ensures no polygons are generated outside the board even for non-rectangular boards.
    border = 10
    outh, outw = original_img.shape
    extended_img = np.zeros((outh + 2*border, outw + 2*border), dtype=np.uint8)
    extended_img[border:outh+border, border:outw+border] = original_img
    cv2.floodFill(extended_img, None, (0, 0), (255,))
    original_img = extended_img[border:outh+border, border:outw+border]
    debugimg(extended_img, 'flooded')

    # Dilate the white areas of the image using gaussian blur and threshold. Use these instead of primitive dilation
    # here for their non-directionality.
    target_img = cv2.blur(original_img, (r, r))
    _, target_img = cv2.threshold(target_img, 255//(1+r), 255, cv2.THRESH_BINARY)
    return target_img

def render_gerbers_to_image(*gerbers, scale, bounds=None):
    with tempfile.TemporaryDirectory() as tmpdir:
        img_file = path.join(tmpdir, 'target.png')
        fg, bg = gerber.render.RenderSettings((1, 1, 1)), gerber.render.RenderSettings((0, 0, 0))
        ctx = GerberCairoContext(scale=scale)

        for grb in gerbers:
            ctx.render_layer(grb, settings=fg, bgsettings=bg, bounds=bounds)

        ctx.dump(img_file)
        # Vertically flip exported image to align coordinate systems
        return cv2.imread(img_file, cv2.IMREAD_GRAYSCALE)[::-1, :]

def pcb_area_mask(outline, scale):
    # Merge layers to target mask
    img = render_gerbers_to_image(outline, scale=scale)
    # Extend
    imgh, imgw = img.shape
    img_ext = np.zeros(shape=(imgh+2, imgw+2), dtype=np.uint8)
    img_ext[1:-1, 1:-1] = img
    # Binarize
    img_ext[img_ext < 128] = 0
    img_ext[img_ext >= 128] = 255
    # Flood-fill
    cv2.floodFill(img_ext, None, (0, 0), (255,)) # Flood-fill with white from top left corner (0,0)
    img_ext_snap = img_ext.copy()
    cv2.floodFill(img_ext, None, (0, 0), (0,)) # Flood-fill with black
    cv2.floodFill(img_ext, None, (0, 0), (255,)) # Flood-fill with white
    return np.logical_xor(img_ext_snap, img_ext)[1:-1, 1:-1].astype(float)

def generate_template(
        silk, mask, copper, outline, drill,
        image,
        gerber_unit=Unit.MM,
        process_resolution:float=6, # mil
        resolution_oversampling:float=8, # times
        debugdir=None,
        status_print=lambda *args:None
        ):

    debugctr = 0
    def debugimg(img, name):
        nonlocal debugctr
        if debugdir:
            cv2.imwrite(path.join(debugdir, '{:02d}{}.png'.format(debugctr, name)), img*255)
        debugctr += 1

    template_scale = (1000/process_resolution) / 25.4 * resolution_oversampling # dpmm

    silk, mask, copper, outline, *drill = map(gerber.loads, [silk, mask, copper, outline, *drill])

    (minx, maxx), (miny, maxy) = outline.bounds
    grbw, grbh = maxx - minx, maxy - miny
    status_print('  * outline has offset {}, size {}'.format((minx, miny), (grbw, grbh)))
    area_mask = pcb_area_mask(outline, template_scale)
    debugimg(area_mask, 'area_mask')
    imgh, imgw = area_mask.shape

    fr4_color       = (0.50, 0.80, 0.50)
    copper_color    = (0.30, 0.50, 0.65)
    mask_color      = (0.15, 0.05, 0.70)
    silk_color      = (0.90, 0.90, 0.90)

    img = np.ones((imgh, imgw, 1)) * fr4_color
    copper_img = render_gerbers_to_image(copper, scale=template_scale, bounds=outline.bounds)
    #copper_img = copper_img.reshape((imgh, imgw, 1)) * copper_color
    debugimg(copper_img.astype(float)/255, 'copper_img')
    #img = np.ones((imgh, imgw, 3)) - (1-img) * (1-copper_img) # screen blend
    img[copper_img != 0, :] = copper_color
    #img = area_mask.reshape((imgh, imgw, 1)) * fr4_color
    debugimg(img, 'up_to_copper')

    mask_img_raw = render_gerbers_to_image(mask, scale=template_scale, bounds=outline.bounds).astype(float)/255
    mask_img = 1 - (1-mask_img_raw.reshape((imgh, imgw, 1))) * (1-np.array(mask_color))
    debugimg(mask_img, 'mask_img')

    img *= mask_img
    debugimg(img, 'up_to_mask')

    silk_img = render_gerbers_to_image(silk, scale=template_scale, bounds=outline.bounds).astype(float)/255 # Invert mask layer
    silk_img *= 1-mask_img_raw
    debugimg(silk_img, 'silk')

    img[silk_img > 0.5, :] = silk_color
    debugimg(img, 'after silk')

    drill_img = render_gerbers_to_image(*drill, scale=template_scale, bounds=outline.bounds).astype(float)/255 # Invert mask layer
    debugimg(drill_img, 'drill')

    img[drill_img > 0.5, :] = (0, 0, 0)

    img[:,:,0] *= area_mask
    img[:,:,1] *= area_mask
    img[:,:,2] *= area_mask
    cv2.imwrite(image, img)
    debugimg(img, 'out_img')
    return img

def paste_image(
        target_gerber:str,
        outline_gerber:str,
        source_img:np.ndarray,
        subtract_gerber:list=[],
        extend_overlay_r_mil:float=6,
        extend_picture_r_mil:float=2,
        status_print=lambda *args:None,
        gerber_unit=Unit.MM,
        debugdir:str=None):

    debugctr = 0
    def debugimg(img, name):
        nonlocal debugctr
        if debugdir:
            cv2.imwrite(path.join(debugdir, '{:02d}{}.png'.format(debugctr, name)), img)
        debugctr += 1

    # Parse outline layer to get bounds of gerber file
    status_print('Parsing outline gerber')
    outline = gerber.loads(outline_gerber)
    (minx, maxx), (miny, maxy) = outline.bounds
    grbw, grbh = maxx - minx, maxy - miny
    status_print('  * outline has offset {}, size {}'.format((minx, miny), (grbw, grbh)))

    # Parse target layer
    status_print('Parsing target gerber')
    target = gerber.loads(target_gerber)
    (tminx, tmaxx), (tminy, tmaxy) = target.bounds
    status_print('  * target layer has offset {}, size {}'.format((tminx, tminy), (tmaxx-tminx, tmaxy-tminy)))

    # Read source image
    imgh, imgw = source_img.shape
    scale = math.ceil(max(imgw/grbw, imgh/grbh)) # scale is in dpmm
    status_print('  * source image has size {}, going for scale {}dpmm'.format((imgw, imgh), scale))

    # Merge layers to target mask
    target_img = generate_mask(outline, target, scale, debugimg, status_print, gerber_unit, extend_overlay_r_mil, subtract_gerber)

    # Threshold source image. Ideally, the source image is already binary but in case it's not, or in case it's not
    # exactly binary (having a few very dark or very light grays e.g. due to JPEG compression) we're thresholding here.
    status_print('Thresholding source image')
    qr = 1+2*max(1, int(extend_picture_r_mil/1000 * scale))
    source_img = source_img[::-1]
    _, source_img = cv2.threshold(source_img, 127, 255, cv2.THRESH_BINARY)
    debugimg(source_img, 'thresh')

    # Pad image to size of target layer images generated above. After this, `scale` applies to the padded image as well
    # as the gerber renders. For padding, zoom or shrink the image to completely fit the gerber's rectangular bounding
    # box. Center the image vertically or horizontally if it has a different aspect ratio.
    status_print('Padding source image')
    tgth, tgtw = target_img.shape
    padded_img = np.zeros(shape=target_img.shape, dtype=source_img.dtype)
    offx = int((minx-tminx if tminx < minx else 0)*scale)
    offy = int((miny-tminy if tminy < miny else 0)*scale)
    offx += int(grbw*scale - imgw) // 2
    offy += int(grbh*scale - imgh) // 2
    endx, endy = min(offx+imgw, tgtw), min(offy+imgh, tgth)
    print('off', (offx, offy), 'end', (endx, endy), 'img', (imgw, imgh), 'tgt', (tgtw, tgth))
    padded_img[offy:endy, offx:endx] = source_img[:endy-offy, :endx-offx]
    debugimg(padded_img, 'padded')
    debugimg(target_img, 'target')

    # Mask out excluded gerber features (source silk, holes, solder mask etc.) from the target image
    status_print('Masking source image')
    out_img = (np.multiply((padded_img/255.0), (target_img/255.0) * -1 + 1) * 255).astype(np.uint8)

    debugimg(out_img, 'multiplied')

    # Calculate contours from masked target image and plot them to the target gerber context
    status_print('Calculating contour lines')
    plot_contours(out_img,
            target,
            offx=(tminx, tminy),
            scale=scale,
            status_print=lambda *args: status_print('   ', *args))

    # Write target gerber context to disk
    status_print('Generating output gerber')
    from gerber.render import rs274x_backend
    ctx = rs274x_backend.Rs274xContext(target.settings)
    target.render(ctx)
    out = ctx.dump().getvalue()
    status_print('Done.')
    return out


def plot_contours(
        img:np.ndarray,
        layer:gerber.rs274x.GerberFile,
        offx:tuple,
        scale:float,
        debug=lambda *args:None,
        status_print=lambda *args:None):
    imgh, imgw = img.shape

    # Extract contour hierarchy using OpenCV
    status_print('Extracting contours')
    img_cont_out, contours, hierarchy = cv2.findContours(img, cv2.RETR_TREE, cv2.CHAIN_APPROX_TC89_KCOS)

    aperture = list(layer.apertures)[0]

    from gerber.primitives import Line, Region
    status_print('offx', offx, 'scale', scale)

    xbias, ybias = offx
    def map(coord):
        x, y = coord
        return (x/scale + xbias, y/scale + ybias)
    def contour_lines(c):
        return [ Line(map(start), map(end), aperture, level_polarity='dark', units=layer.settings.units)
            for start, end in zip(c, np.vstack((c[1:], c[:1]))) ]

    done = []
    process_stack = [-1]
    next_process_stack = []
    parents = [ (i, first_child != -1, parent) for i, (_1, _2, first_child, parent) in enumerate(hierarchy[0]) ]
    is_dark = True
    status_print('Converting contours to gerber primitives')
    with tqdm.tqdm(total=len(contours)) as progress:
        while len(done) != len(contours):
            for i, has_children, parent in parents[:]:
                if parent in process_stack:
                    contour = contours[i]
                    polarity = 'dark' if is_dark else 'clear'
                    debug('rendering {} with parent {} as {} with {} vertices'.format(i, parent, polarity, len(contour)))
                    debug('process_stack is', process_stack)
                    debug()
                    layer.primitives.append(Region(contour_lines(contour[:,0]), level_polarity=polarity, units=layer.settings.units))
                    if has_children:
                        next_process_stack.append(i)
                    done.append(i)
                    parents.remove((i, has_children, parent))
                    progress.update(1)
            debug('skipping to next level')
            process_stack, next_process_stack = next_process_stack, []
            is_dark = not is_dark
    debug('done', done)

# Utility foo
# ===========

def find_gerber_in_dir(dir_path, extensions, exclude=''):
    contents = os.listdir(dir_path)
    exts = extensions.split('|')
    excs = exclude.split('|')
    for entry in contents:
        if any(entry.lower().endswith(ext.lower()) for ext in exts) and not any(entry.lower().endswith(ex) for ex in excs if exclude):
            lname = path.join(dir_path, entry)
            if not path.isfile(lname):
                continue
            with open(lname, 'r') as f:
                return lname, f.read()

    raise ValueError(f'Cannot find file with suffix {extensions} in dir {dir_path}')

# Gerber file name extensions for Altium/Protel | KiCAD | Eagle
LAYER_SPEC = {
        'top': {
            'paste':    '.gtp|-F.Paste.gbr|.pmc',
            'silk':     '.gto|-F.SilkS.gbr|.plc',
            'mask':     '.gts|-F.Mask.gbr|.stc',
            'copper':   '.gtl|-F.Cu.bgr|.cmp',
            'outline':  '.gm1|-Edge.Cuts.gbr|.gmb',
        },
        'bottom': {
            'paste':    '.gbp|-B.Paste.gbr|.pms',
            'silk':     '.gbo|-B.SilkS.gbr|.pls',
            'mask':     '.gbs|-B.Mask.gbr|.sts',
            'copper':   '.gbl|-B.Cu.bgr|.sol',
            'outline':  '.gm1|-Edge.Cuts.gbr|.gmb'
        },
    }

# Command line interface
# ======================

def process_gerbers(source, target, image, side, layer, debugdir):
    if not os.path.isdir(source):
        raise ValueError(f'Given source "{source}" is not a directory.')

    # Load input files
    source_img = cv2.imread(image, cv2.IMREAD_GRAYSCALE)
    if source_img is None:
        print(f'"{image}" is not a valid image file', file=sys.stderr)
        sys.exit(1)

    tlayer, slayer = {
            'silk': ('silk', 'mask'),
            'mask': ('mask', 'silk'),
            'copper': ('copper', None)
            }[layer]

    layers = LAYER_SPEC[side]
    tname, tgrb = find_gerber_in_dir(source, layers[tlayer])
    print('Target layer file {}'.format(os.path.basename(tname)))
    oname, ogrb  = find_gerber_in_dir(source, layers['outline'])
    print('Outline layer file {}'.format(os.path.basename(oname)))
    subtract = find_gerber_in_dir(source, layers[slayer]) if slayer else None

    # Prepare output. Do this now to error out as early as possible if there's a problem.
    if os.path.exists(target):
        if os.path.isdir(target) and sorted(os.listdir(target)) == sorted(os.listdir(source)):
            shutil.rmtree(target)
        else:
            print('Error: Target already exists and does not look like source. Please manually remove the target dir before proceeding.', file=sys.stderr)
            sys.exit(1)

    # Generate output
    out = paste_image(tgrb, ogrb, source_img, [subtract], debugdir=debugdir, status_print=lambda *args: print(*args, flush=True))

    shutil.copytree(source, target)
    with open(os.path.join(target, os.path.basename(tname)), 'w') as f:
        f.write(out)

def render_preview(source, image, side, debugdir=None):
    def load_layer(layer):
        name, grb = find_gerber_in_dir(source, LAYER_SPEC[side][layer])
        print(f'{layer} layer file {os.path.basename(name)}')
        return grb
    
    outline = load_layer('outline')
    silk = load_layer('silk')
    mask = load_layer('mask')
    copper = load_layer('copper')

    try:
        _, npth = find_gerber_in_dir(source, '-npth.drl')
    except ValueError:
        npth = None
    drill = ([npth] if npth else []) + [find_gerber_in_dir(source, '.drl|.txt', exclude='-npth.drl')[1]]
    
    generate_template(
        silk, mask, copper, outline, drill,
        image,
        gerber_unit=Unit.MM,
        process_resolution=6, # mil
        resolution_oversampling=8, # times
        debugdir=debugdir
        )

if __name__ == '__main__':
    # Parse command line arguments
    import argparse
    parser = argparse.ArgumentParser()

    subcommand = parser.add_subparsers(help='Sub-commands')
    subcommand.required, subcommand.dest = True, 'command'
    vectorize_parser = subcommand.add_parser('vectorize', help='Vectorize bitmap image onto gerber layer')
    render_parser = subcommand.add_parser('render', help='Render bitmap preview of board suitable as a template for positioning and scaling the input image')

    parser.add_argument('-d', '--debugdir', type=str, default=None, help='Directory to place intermediate images into for debuggin')

    vectorize_parser.add_argument('side', choices=['top', 'bottom'], help='Target board side')
    vectorize_parser.add_argument('--layer', '-l', choices=['silk', 'mask', 'copper'], default='silk', help='Target layer on given side')

    vectorize_parser.add_argument('source', help='Source gerber directory')
    vectorize_parser.add_argument('target', help='Target gerber directory')
    vectorize_parser.add_argument('image', help='Image to render')

    render_parser.add_argument('side', choices=['top', 'bottom'], help='Target board side')
    render_parser.add_argument('source', help='Source gerber directory')
    render_parser.add_argument('image', help='Output image filename')
    args = parser.parse_args()

    #try:
    if args.command == 'vectorize':
        process_gerbers(args.source, args.target, args.image, args.side, args.layer, args.debugdir)
    else: # command == render
        render_preview(args.source, args.image, args.side, args.debugdir)
    #except ValueError as e:
    #    print(*e.args, file=sys.stderr)
    #    sys.exit(1)

