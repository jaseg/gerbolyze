/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2021 Jan Sebastian Götte <kicad@jaseg.de>
 * Copyright (C) 2021 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <assert.h>
#include "svg_import_util.h"
#include "svg_pattern.h"
#include "svg_import_defs.h"
#include "svg_geom.h"
#include "svg_doc.h"

using namespace std;

svg_plugin::Pattern::Pattern(const pugi::xml_node &node, SVGDocument &doc) : _node(node), doc(&doc) {
    /* Read pattern attributes from SVG node */
    cerr << "creating pattern for node with id \"" << node.attribute("id").value() << "\"" << endl;
    x = usvg_double_attr(node, "x");
    y = usvg_double_attr(node, "y");
    w = usvg_double_attr(node, "width");
    h = usvg_double_attr(node, "height");
    patternTransform = node.attribute("patternTransform").value();

    string vb_s(node.attribute("viewBox").value());
    has_vb = !vb_s.empty();
    if (has_vb) {
        istringstream vb_stream(vb_s);
        vb_stream >> vb_x >> vb_y >> vb_w >> vb_h;
    }

    patternUnits = map_str_to_units(node.attribute("patternUnits").value(), SVG_ObjectBoundingBox);
    patternContentUnits = map_str_to_units(node.attribute("patternContentUnits").value(), SVG_UserSpaceOnUse);
}

/* Tile pattern into gerber. Note that this function may be called several times in case the pattern is
 * referenced from multiple places, so we must not clobber any of the object's state. */
void svg_plugin::Pattern::tile (ClipperLib::Paths &clip) {
    assert(doc);
    cairo_t *cr = doc->cairo();
    assert(cr);

    cairo_save(cr);
    /* Transform x, y, w, h from pattern coordinate space into parent coordinates by applying the inverse
     * patternTransform. This is necessary so we iterate over the correct bounds when tiling below */
    cairo_matrix_t mat;
    load_cairo_matrix_from_svg(patternTransform, mat);
    if (cairo_matrix_invert(&mat) != CAIRO_STATUS_SUCCESS) {
        cerr << "Cannot invert patternTransform matrix on pattern \"" << _node.attribute("id").value() << "\"." << endl;
        cairo_restore(cr);
    }
    double inst_x = x, inst_y = y, inst_w = w, inst_h = h;
    cairo_user_to_device(cr, &inst_x, &inst_y);
    cairo_user_to_device_distance(cr, &inst_w, &inst_h);
    cairo_restore(cr);

    ClipperLib::IntRect clip_bounds = get_paths_bounds(clip);
    double bx = clip_bounds.left / clipper_scale;
    double by = clip_bounds.top / clipper_scale;
    double bw = (clip_bounds.right - clip_bounds.left) / clipper_scale;
    double bh = (clip_bounds.bottom - clip_bounds.top) / clipper_scale;

    if (patternUnits == SVG_ObjectBoundingBox) {
        inst_x *= bw;
        inst_y *= bh;
        inst_w *= bw;
        inst_h *= bh;
    }

    /* Switch to pattern coordinates */
    cairo_save(cr);
    cairo_translate(cr, bx, by);
    apply_cairo_transform_from_svg(cr, patternTransform);

    /* Iterate over all pattern tiles in pattern coordinates */
    for (double inst_off_x = fmod(inst_x, inst_w) - inst_w;
            inst_off_x < bw + inst_w;
            inst_off_x += inst_w) {

        for (double inst_off_y = fmod(inst_y, inst_h) - inst_h;
                inst_off_y < bh + inst_h;
                inst_off_y += inst_h) {

            cairo_save(cr);
            /* Change into this individual tile's coordinate system */
            cairo_translate(cr, inst_off_x, inst_off_y);
            if (has_vb) {
                cairo_translate(cr, vb_x, vb_y);
                cairo_scale(cr, inst_w / vb_w, inst_h / vb_h);
            } else if (patternContentUnits == SVG_ObjectBoundingBox) {
                cairo_scale(cr, bw, bh);
            }

            /* Export the pattern tile's content like a group */
            doc->export_svg_group(_node, clip);
            cairo_restore(cr);
        }
    }
    cairo_restore(cr);
}

