/*
 * This file is part of gerbolyze, a vector image preprocessing toolchain 
 * Copyright (C) 2021 Jan Sebastian Götte <gerbolyze@jaseg.de>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include "svg_import_util.h"
#include "svg_pattern.h"
#include "svg_import_defs.h"
#include "svg_geom.h"
#include <gerbolyze.hpp>

using namespace std;

gerbolyze::Pattern::Pattern(const pugi::xml_node &node, SVGDocument &doc) : _node(node), doc(&doc) {
    /* Read pattern attributes from SVG node */
    cerr << "creating pattern for node with id \"" << node.attribute("id").value() << "\"" << endl;
    x = usvg_double_attr(node, "x");
    y = usvg_double_attr(node, "y");
    w = usvg_double_attr(node, "width");
    h = usvg_double_attr(node, "height");

    patternTransform = xform2d(node.attribute("patternTransform").value());

    bool invert_success = false;
    patternTransform_inv = xform2d(patternTransform).invert(&invert_success);
    if (!invert_success) {
        cerr << "Warning: Cannot invert patternTransform matrix on pattern \"" << node.attribute("id").value() << "\"." << endl;
    }

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
void gerbolyze::Pattern::tile (xform2d &mat, const gerbolyze::RenderSettings &rset, ClipperLib::Paths &clip) {
    assert(doc);

    /* Transform x, y, w, h from pattern coordinate space into parent coordinates by applying the inverse
     * patternTransform. This is necessary so we iterate over the correct bounds when tiling below */
    d2p pos_xf = mat.doc2phys(d2p{x, y});
    double inst_x = pos_xf[0], inst_y = pos_xf[1];
    double inst_w = mat.doc2phys_dist(w);
    double inst_h = mat.doc2phys_dist(h);

    ClipperLib::IntRect clip_bounds = get_paths_bounds(clip);
    double bx = clip_bounds.left / clipper_scale;
    double by = clip_bounds.top / clipper_scale;
    double bw = (clip_bounds.right - clip_bounds.left) / clipper_scale;
    double bh = (clip_bounds.bottom - clip_bounds.top) / clipper_scale;

    d2p clip_p0 = patternTransform_inv.doc2phys(d2p{bx, by});
    d2p clip_p1 = patternTransform_inv.doc2phys(d2p{bx+bw, by+bh});

    bx = fmin(clip_p0[0], clip_p1[0]);
    by = fmin(clip_p0[1], clip_p1[1]);
    bw = fmax(clip_p0[0], clip_p1[0]) - bx;
    bh = fmax(clip_p0[1], clip_p1[1]) - by;

    if (patternUnits == SVG_ObjectBoundingBox) {
        inst_x *= bw;
        inst_y *= bh;
        inst_w *= bw;
        inst_h *= bh;
    }

    /* Switch to pattern coordinates */
    xform2d local_xf(mat);
    local_xf.transform(patternTransform);

    /* Iterate over all pattern tiles in pattern coordinates */
    for (double inst_off_x = fmod(inst_x, inst_w) - 2*inst_w;
            inst_off_x < bw + 2*inst_w;
            inst_off_x += inst_w) {

        for (double inst_off_y = fmod(inst_y, inst_h) - 2*inst_h;
                inst_off_y < bh + 2*inst_h;
                inst_off_y += inst_h) {

            xform2d elem_xf(local_xf);
            /* Change into this individual tile's coordinate system */
            elem_xf.translate(inst_off_x, inst_off_y);
            if (has_vb) {
                elem_xf.translate(vb_x, vb_y);
                elem_xf.scale(inst_w / vb_w, inst_h / vb_h);
            } else if (patternContentUnits == SVG_ObjectBoundingBox) {
                elem_xf.scale(bw, bh);
            }

            /* Export the pattern tile's content like a group */
            doc->export_svg_group(elem_xf, rset, _node, clip);
        }
    }
}

