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

gerbolyze::Pattern::Pattern(const pugi::xml_node &node, SVGDocument &doc) : m_node(node), doc(&doc) {
    /* Read pattern attributes from SVG node */
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
void gerbolyze::Pattern::tile (gerbolyze::RenderContext &ctx) {
    assert(doc);

    /* Transform x, y, w, h from pattern coordinate space into parent coordinates by applying the inverse
     * patternTransform. This is necessary so we iterate over the correct bounds when tiling below */
    d2p pos_xf = ctx.mat().doc2phys(d2p{x, y});
    double inst_x = pos_xf[0], inst_y = pos_xf[1];
    double inst_w = w;
    double inst_h = h;

    ClipperLib::IntRect clip_bounds = get_paths_bounds(ctx.clip());
    double bx = clip_bounds.left / clipper_scale;
    double by = clip_bounds.top / clipper_scale;
    double bw = (clip_bounds.right - clip_bounds.left) / clipper_scale;
    double bh = (clip_bounds.bottom - clip_bounds.top) / clipper_scale;

    d2p clip_p0 = patternTransform_inv.doc2phys(d2p{bx, by});
    d2p clip_p1 = patternTransform_inv.doc2phys(d2p{bx+bw, by});
    d2p clip_p2 = patternTransform_inv.doc2phys(d2p{bx+bw, by+bh});
    d2p clip_p3 = patternTransform_inv.doc2phys(d2p{bx, by+bh});

    bx = fmin(fmin(clip_p0[0], clip_p1[0]), fmin(clip_p2[0], clip_p3[0]));
    by = fmin(fmin(clip_p0[1], clip_p1[1]), fmin(clip_p2[1], clip_p3[1]));
    bw = fmax(fmax(clip_p0[0], clip_p1[0]), fmax(clip_p2[0], clip_p3[0])) - bx;
    bh = fmax(fmax(clip_p0[1], clip_p1[1]), fmax(clip_p2[1], clip_p3[1])) - by;

    if (patternUnits == SVG_ObjectBoundingBox) {
        inst_x *= bw;
        inst_y *= bh;
        inst_w *= bw;
        inst_h *= bh;
    }

    /* Switch to pattern coordinates */
    RenderContext pat_ctx(ctx, patternTransform);

    if (ctx.settings().use_apertures_for_patterns) {
        vector<pair<Polygon, GerberPolarityToken>> out;
        LambdaPolygonSink list_sink([&out](const Polygon &poly, GerberPolarityToken pol) {
                out.emplace_back(pair<Polygon, GerberPolarityToken>{poly, pol});
            });
        ClipperLib::Paths empty_clip;
        RenderContext macro_ctx(pat_ctx, list_sink, empty_clip);
        doc->export_svg_group(macro_ctx, m_node);
        pat_ctx.sink() << PatternToken(out);
    }

    /* Iterate over all pattern tiles in pattern coordinates */
    for (double inst_off_x = fmod(inst_x, inst_w) - 2*inst_w;
            inst_off_x < bx + bw + 2*inst_w;
            inst_off_x += inst_w) {
        for (double inst_off_y = fmod(inst_y, inst_h) - 2*inst_h;
                inst_off_y < by + bh + 2*inst_h;
                inst_off_y += inst_h) {
            xform2d elem_xf;
            /* Change into this individual tile's coordinate system */
            elem_xf.translate(inst_off_x, inst_off_y);
            if (has_vb) {
                elem_xf.translate(vb_x, vb_y);
                elem_xf.scale(inst_w / vb_w, inst_h / vb_h);
            } else if (patternContentUnits == SVG_ObjectBoundingBox) {
                elem_xf.scale(bw, bh);
            }

            /* Export the pattern tile's content like a group */
            RenderContext elem_ctx(pat_ctx, elem_xf);

            if (ctx.settings().pattern_complete_tiles_only) {
                ClipperLib::Clipper c;

                double eps = 1e-6;
                Polygon poly = {{eps, eps}, {inst_w-eps, eps}, {inst_w-eps, inst_h-eps}, {eps, inst_h-eps}};
                elem_ctx.mat().transform_polygon(poly);
                ClipperLib::Path path(poly.size());
                for (size_t i=0; i<poly.size(); i++) {
                    long long int x = poly[i][0] * clipper_scale, y = poly[i][1] * clipper_scale;
                    path[i] = {x, y};
                }

                ClipperLib::Paths out;
                c.StrictlySimple(true);
                c.AddPath(path, ClipperLib::ptSubject, /* closed */ true);
                c.AddPaths(elem_ctx.clip(), ClipperLib::ptClip, /* closed */ true);
                c.Execute(ClipperLib::ctDifference, out, ClipperLib::pftNonZero);
                if (out.size() > 0) {
                    continue;
                }
            }

            if (ctx.settings().use_apertures_for_patterns) {
                /* use inst_h offset to compensate for gerber <-> svg "y" coordinate spaces */
                elem_ctx.sink() << FlashToken(elem_ctx.mat().doc2phys({0, inst_h}));
            } else {
                doc->export_svg_group(elem_ctx, m_node);
            }
        }
    }
}

