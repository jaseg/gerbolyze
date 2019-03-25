#!/usr/bin/env python3

import tempfile
import os.path as path
import os
import sys
import time
import shutil
import math

import gerber
from gerber.render.cairo_backend import GerberCairoContext
import numpy as np
import cv2
import enum
import tqdm

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
        resolution_oversampling:float=10, # times
        status_print=lambda *args:None
        ):

    silk, mask, copper, outline, *drill = map(gerber.load_layer_data, [silk, mask, copper, outline, *drill])
    silk.layer_class = 'topsilk'
    mask.layer_class = 'topmask'
    copper.layer_class = 'top'
    outline.layer_class = 'outline'
    scale = (1000/process_resolution) / 25.4 * resolution_oversampling # dpmm

    # Create a new drawing context
    ctx = GerberCairoContext(scale=scale)

    ctx.render_layer(outline)
    ctx.render_layer(copper)
    ctx.render_layer(mask)
    ctx.render_layer(silk)
    for dr in drill:
        ctx.render_layer(dr)
    ctx.dump(image)

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
            offx=(minx, miny),
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
    # See https://stackoverflow.com/questions/48291581/how-to-use-cv2-findcontours-in-different-opencv-versions/48292371
    contours, hierarchy = cv2.findContours(img, cv2.RETR_TREE, cv2.CHAIN_APPROX_TC89_KCOS)[-2:]

    aperture = list(layer.apertures)[0]

    from gerber.primitives import Line, Region
    status_print('offx', offx, 'scale', scale)

    xbias, ybias = offx
    def map(coord):
        x, y = coord
        return (x/scale + xbias, y/scale + ybias)
    def contour_lines(c):
        return [ Line(map(start), map(end), aperture, units=layer.settings.units)
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
            'copper':   '.gtl|-F.Cu.gbr|.cmp',
            'outline':  '.gm1|-Edge.Cuts.gbr|.gmb',
        },
        'bottom': {
            'paste':    '.gbp|-B.Paste.gbr|.pms',
            'silk':     '.gbo|-B.SilkS.gbr|.pls',
            'mask':     '.gbs|-B.Mask.gbr|.sts',
            'copper':   '.gbl|-B.Cu.gbr|.sol',
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

def render_preview(source, image, side, process_resolution, resolution_oversampling):
    def load_layer(layer):
        name, grb = find_gerber_in_dir(source, LAYER_SPEC[side][layer])
        print(f'{layer} layer file {os.path.basename(name)}')
        return grb
    
    outline = load_layer('outline')
    silk = load_layer('silk')
    mask = load_layer('mask')
    copper = load_layer('copper')

    try:
        nm, npth = find_gerber_in_dir(source, '-npth.drl')
        print(f'npth drill file {nm}')
    except ValueError:
        npth = None
    nm, drill = find_gerber_in_dir(source, '.drl|.txt', exclude='-npth.drl')
    print(f'drill file {nm}')
    drill = ([npth] if npth else []) + [drill]
    
    generate_template(
        silk, mask, copper, outline, drill,
        image,
        gerber_unit=Unit.MM,
        process_resolution=process_resolution,
        resolution_oversampling=resolution_oversampling,
        )

