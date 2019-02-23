#!/usr/bin/env python3

import os
import math
import shutil
import logging
from os.path import join
from typing import Tuple
from tempfile import TemporaryDirectory

import cv2
import numpy as np
from tqdm import tqdm
import gerber
from gerber.primitives import Line, Region
from gerber.render import RenderSettings, rs274x_backend
from gerber.render.cairo_backend import GerberCairoContext


from gerbolyze.util import find_gerber_in_dir, LAYER_SPEC, GerbolyzeError


def generate_mask(outline, target, scale, debugimg, extend_overlay_r_mil, subtract_gerber: Tuple[str, str] = None,
                  inch_as_unit: bool = False):
    """
    Render all gerber layers whose features are to be excluded from the target image, such as board outline, the
    original silk layer and the solder paste layer to binary images.

    :param outline:
    :param target:
    :param scale:
    :param debugimg:
    :param extend_overlay_r_mil:
    :param subtract_gerber:
    :param inch_as_unit: Use inches instead of mm
    :return:
    """
    with TemporaryDirectory() as tmpdir:
        img_file = join(tmpdir, 'target.png')
        logging.info('Combining keepout composite')
        fg, bg = RenderSettings((1, 1, 1)), RenderSettings((0, 0, 0))
        ctx = GerberCairoContext(scale=scale)
        logging.info('outline')
        ctx.render_layer(outline, settings=fg, bgsettings=bg)
        logging.info('target layer')
        ctx.render_layer(target, settings=fg, bgsettings=bg)
        if subtract_gerber:
            fn, sub = subtract_gerber
            logging.info(f'extra layer { os.path.basename(fn) }')
            layer = gerber.loads(sub)
            ctx.render_layer(layer, settings=fg, bgsettings=bg)
        logging.info('Rendering keepout composite')
        ctx.dump(img_file)

        # Vertically flip exported image
        original_img = cv2.imread(img_file, cv2.IMREAD_GRAYSCALE)[::-1, :]

    if inch_as_unit:
        f = 1
    else:
        f = 25.4  # MM

    r = 1 + 2 * max(1, int(extend_overlay_r_mil / 1000 * f * scale))
    logging.info(f'Expanding keepout composite by { r }{ "inch" if inch_as_unit else "mm" }')

    # Extend image by a few pixels and flood-fill from (0, 0) to mask out the area outside the outermost outline
    # This ensures no polygons are generated outside the board even for non-rectangular boards.
    border = 10
    outh, outw = original_img.shape
    extended_img = np.zeros((outh + 2 * border, outw + 2 * border), dtype=np.uint8)
    extended_img[border:outh + border, border:outw + border] = original_img
    cv2.floodFill(extended_img, None, (0, 0), (255,))
    original_img = extended_img[border:outh + border, border:outw + border]
    debugimg(extended_img, 'flooded')

    # Dilate the white areas of the image using gaussian blur and threshold. Use these instead of primitive dilation
    # here for their non-directionality.
    return cv2.threshold(
        cv2.blur(original_img, (r, r)),
        255 // (1 + r),
        255,
        cv2.THRESH_BINARY
    )[1]


def render_gerbers_to_image(*gerbers, scale, bounds=None):
    with TemporaryDirectory() as tmpdir:
        img_file = join(tmpdir, 'target.png')
        fg, bg = RenderSettings((1, 1, 1)), RenderSettings((0, 0, 0))
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
    img_ext = np.zeros(shape=(imgh + 2, imgw + 2), dtype=np.uint8)
    img_ext[1:-1, 1:-1] = img
    # Binarize
    img_ext[img_ext < 128] = 0
    img_ext[img_ext >= 128] = 255
    # Flood-fill
    cv2.floodFill(img_ext, None, (0, 0), (255,))  # Flood-fill with white from top left corner (0,0)
    img_ext_snap = img_ext.copy()
    cv2.floodFill(img_ext, None, (0, 0), (0,))  # Flood-fill with black
    cv2.floodFill(img_ext, None, (0, 0), (255,))  # Flood-fill with white

    return np.logical_xor(img_ext_snap, img_ext)[1:-1, 1:-1].astype(float)


def generate_template(silk, mask, copper, outline, drill, image, process_resolution: float = 6.0,
                      resolution_oversampling: float = 10.0):
    """

    :param silk:
    :param mask:
    :param copper:
    :param outline:
    :param drill:
    :param image:
    :param process_resolution:  mm
    :param resolution_oversampling: n times
    :return:
    """
    silk, mask, copper, outline, *drill = map(
        gerber.load_layer_data,
        [
            silk,
            mask,
            copper,
            outline,
            *drill
        ]
    )
    silk.layer_class = 'topsilk'
    mask.layer_class = 'topmask'
    copper.layer_class = 'top'
    outline.layer_class = 'outline'
    scale = (1000 / process_resolution) / 25.4 * resolution_oversampling  # dpmm

    # Create a new drawing context
    ctx = GerberCairoContext(scale=scale)

    ctx.render_layer(outline)
    ctx.render_layer(copper)
    ctx.render_layer(mask)
    ctx.render_layer(silk)
    for dr in drill:
        ctx.render_layer(dr)
    ctx.dump(image)


# extend_picture_r_mil: float = 2.0
def paste_image(target_gerber: str, outline_gerber: str, source_img: np.ndarray,
                subtract_gerber: Tuple[str, str] = None, extend_overlay_r_mil: float = 6, debugdir: str = None):
    """

    :param target_gerber:
    :param outline_gerber:
    :param source_img:
    :param subtract_gerber:
    :param extend_overlay_r_mil:
    :param extend_picture_r_mil:
    :param debugdir:
    :return:
    """
    debugctr = 0
    def debugimg(img, name):
        nonlocal debugctr
        if debugdir:
            cv2.imwrite(join(debugdir, '{:02d}{}.png'.format(debugctr, name)), img)
        debugctr += 1

    # Parse outline layer to get bounds of gerber file
    logging.info('Parsing outline gerber')
    outline = gerber.loads(outline_gerber)
    (minx, maxx), (miny, maxy) = outline.bounds
    grbw, grbh = maxx - minx, maxy - miny
    logging.info(f'outline has offset { minx }, { miny }, size { grbw }, { grbh }')

    # Parse target layer
    logging.info('Parsing target gerber')
    target = gerber.loads(target_gerber)
    (tminx, tmaxx), (tminy, tmaxy) = target.bounds
    logging.info(f'target layer has offset { tminx }, { tminy }, size { tmaxx - tminx }, { tmaxy - tminy }')

    # Read source image
    imgh, imgw = source_img.shape
    scale = math.ceil(max(imgw / grbw, imgh / grbh))  # scale is in dpmm
    logging.info(f'source image has size { imgw }, { imgh }, going for scale { scale }dpmm')

    # Merge layers to target mask
    target_img = generate_mask(outline, target, scale, debugimg, extend_overlay_r_mil, subtract_gerber)

    # Threshold source image. Ideally, the source image is already binary but in case it's not, or in case it's not
    # exactly binary (having a few very dark or very light grays e.g. due to JPEG compression) we're thresholding here.
    logging.info('Thresholding source image')
    #qr = 1 + 2 * max(1, int(extend_picture_r_mil / 1000 * scale))
    source_img = source_img[::-1]
    _, source_img = cv2.threshold(source_img, 127, 255, cv2.THRESH_BINARY)
    debugimg(source_img, 'thresh')

    # Pad image to size of target layer images generated above. After this, `scale` applies to the padded image as well
    # as the gerber renders. For padding, zoom or shrink the image to completely fit the gerber's rectangular bounding
    # box. Center the image vertically or horizontally if it has a different aspect ratio.
    logging.info('Padding source image')
    tgth, tgtw = target_img.shape
    padded_img = np.zeros(shape=target_img.shape, dtype=source_img.dtype)
    offx = int((minx - tminx if tminx < minx else 0)*scale)
    offy = int((miny - tminy if tminy < miny else 0)*scale)
    offx += int(grbw * scale - imgw) // 2
    offy += int(grbh * scale - imgh) // 2
    endx, endy = min(offx + imgw, tgtw), min(offy + imgh, tgth)
    logging.info(f'off ({ offx }, { offy }), end ({ endx }, { endy }), img ({ imgw }, { imgh }), tgt ({ tgtw }, { tgth })')
    padded_img[offy:endy, offx:endx] = source_img[:endy - offy, :endx - offx]
    debugimg(padded_img, 'padded')
    debugimg(target_img, 'target')

    # Mask out excluded gerber features (source silk, holes, solder mask etc.) from the target image
    logging.info('Masking source image')
    out_img = (np.multiply((padded_img / 255.0), (target_img / 255.0) * -1 + 1) * 255).astype(np.uint8)

    debugimg(out_img, 'multiplied')

    # Calculate contours from masked target image and plot them to the target gerber context
    logging.info('Calculating contour lines')
    plot_contours(out_img, target, offx=(minx, miny), scale=scale)

    # Write target gerber context to disk
    logging.info('Generating output gerber')
    ctx = rs274x_backend.Rs274xContext(target.settings)
    target.render(ctx)
    out = ctx.dump().getvalue()
    logging.info('Done.')

    return out


def plot_contours(img: np.ndarray, layer: gerber.rs274x.GerberFile, offx: tuple, scale: float):
    def map_coord(coord):
        x, y = coord
        return (x / scale + xbias, y / scale + ybias)

    def contour_lines(c):
        return [Line(map_coord(start), map_coord(end), aperture, units=layer.settings.units)
                for start, end in zip(c, np.vstack((c[1:], c[:1])))]

    # Extract contour hierarchy using OpenCV
    logging.debug('Extracting contours')
    img_cont_out, contours, hierarchy = cv2.findContours(img, cv2.RETR_TREE, cv2.CHAIN_APPROX_TC89_KCOS)

    aperture = list(layer.apertures)[0]

    logging.debug('offx', offx, 'scale', scale)

    xbias, ybias = offx

    done = []
    process_stack = [-1]
    next_process_stack = []
    parents = [ (i, first_child != -1, parent) for i, (_1, _2, first_child, parent) in enumerate(hierarchy[0]) ]
    is_dark = True
    logging.debug('Converting contours to gerber primitives')
    # TODO: remove progress bar when used as lib
    with tqdm(total=len(contours)) as progress:
        while len(done) != len(contours):
            for i, has_children, parent in parents[:]:
                if parent in process_stack:
                    contour = contours[i]
                    polarity = 'dark' if is_dark else 'clear'
                    logging.debug(f'rendering { i } with parent { parent } as { polarity } with { len(contour) } vertices')
                    logging.debug(f'process_stack is { process_stack }')
                    layer.primitives.append(Region(contour_lines(contour[:, 0]), level_polarity=polarity, units=layer.settings.units))
                    if has_children:
                        next_process_stack.append(i)
                    done.append(i)
                    parents.remove((i, has_children, parent))
                    progress.update(1)
            logging.debug('skipping to next level')
            process_stack, next_process_stack = next_process_stack, []
            is_dark = not is_dark
    logging.debug('done', done)


def process_gerbers(source: str, target: str, image: str, side: str, layer: str, debugdir: str):
    """

    :param source: Directory containing the gerber files
    :param target:
    :param image:
    :param side:
    :param layer:
    :param debugdir:
    :return:
    """
    if not os.path.isdir(source):
        raise GerbolyzeError(f'Given source "{ source }" is not a directory.')

    # Load input files
    source_img = cv2.imread(image, cv2.IMREAD_GRAYSCALE)
    if source_img is None:
        raise GerbolyzeError(f'"{ image }" is not a valid image file')

    tlayer, slayer = {
        'silk': ('silk', 'mask'),
        'mask': ('mask', 'silk'),
        'copper': ('copper', None)
    }[layer]

    layers = LAYER_SPEC[side]
    tname, tgrb = find_gerber_in_dir(source, layers[tlayer])
    logging.info(f'Target layer file { os.path.basename(tname) }')
    oname, ogrb  = find_gerber_in_dir(source, layers['outline'])
    logging.info(f'Outline layer file { os.path.basename(oname) }')
    subtract = find_gerber_in_dir(source, layers[slayer]) if slayer else None

    # Prepare output. Do this now to error out as early as possible if there's a problem.
    if os.path.isdir(target):
        if sorted(os.listdir(target)) == sorted(os.listdir(source)):
            shutil.rmtree(target)
        else:
            raise GerbolyzeError('Error: Target already exists and does not look like source. Please manually remove the target dir before proceeding.')

    # Generate output
    out = paste_image(tgrb, ogrb, source_img, subtract, debugdir=debugdir)

    shutil.copytree(source, target)
    with open(join(target, os.path.basename(tname)), 'w') as f:
        f.write(out)


def render_preview(source: str, image: str, side: str, process_resolution: float, resolution_oversampling: float):
    """

    :param source:
    :param image:
    :param side:
    :param process_resolution:
    :param resolution_oversampling:
    :return:
    """
    def load_layer(layer):
        name, grb = find_gerber_in_dir(source, LAYER_SPEC[side][layer])
        logging.debug(f'{ layer } layer file { os.path.basename(name) }')

        return grb
    outline = load_layer('outline')
    silk = load_layer('silk')
    mask = load_layer('mask')
    copper = load_layer('copper')

    try:
        nm, npth = find_gerber_in_dir(source, extensions=['-npth.drl', ])
        print(f'npth drill file { nm }')
    except FileNotFoundError:
        npth = None
    nm, drill = find_gerber_in_dir(source, extensions=['.drl', '.txt'], exclude=['-npth.drl', ])
    print(f'drill file { nm }')
    drill = ([npth] if npth else []) + [drill]
    
    generate_template(
        silk, mask, copper, outline, drill,
        image,
        process_resolution=process_resolution,
        resolution_oversampling=resolution_oversampling,
    )
