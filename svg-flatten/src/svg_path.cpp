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

#include <cmath>
#include <assert.h>
#include <iostream>
#include <iomanip>
#include <sstream>

#include "svg_import_defs.h"
#include "svg_path.h"
#include "flatten.hpp"

using namespace std;

static pair<bool, bool> flatten_path(gerbolyze::xform2d &mat, ClipperLib::Clipper &c_stroke, ClipperLib::Clipper &c_fill, const pugi::char_t *path_data, double distance_tolerance_mm) {
    istringstream in(path_data);

    string cmd;
    gerbolyze::d2p a, b, c, d;

    ClipperLib::Path in_poly;

    bool first = true;
    bool has_closed = false;
    int num_subpaths = 0;
    while (!in.eof()) {
        in >> cmd;
        assert (!in.fail());
        assert(!first || cmd == "M");
        
        if (cmd == "Z") { /* Close path */
            c_stroke.AddPath(in_poly, ClipperLib::ptSubject, true);
            c_fill.AddPath(in_poly, ClipperLib::ptSubject, true);

            has_closed = true;
            in_poly.clear();
            num_subpaths += 1;

        } else if (cmd == "M") { /* Move to */
            if (!first && !in_poly.empty()) {
                c_stroke.AddPath(in_poly, ClipperLib::ptSubject, false);
                c_fill.AddPath(in_poly, ClipperLib::ptSubject, true);
                num_subpaths += 1;
                in_poly.clear();
            }

            in >> a[0] >> a[1];
            assert (!in.fail()); /* guaranteed by usvg */

            /* We need to transform all points ourselves here, and cannot use the transform feature of cairo_to_clipper:
             * Our transform may contain offsets, and clipper only passes its data into cairo's transform functions
             * after scaling up to its internal fixed-point ints, but it does not scale the transform accordingly. This
             * means a scale/rotation we set before calling clipper works out fine, but translations get lost as they
             * get scaled by something like 1e-6.
             */
            a = mat.doc2phys(a);

            in_poly.emplace_back(ClipperLib::IntPoint{
                    (ClipperLib::cInt)round(a[0]*clipper_scale),
                    (ClipperLib::cInt)round(a[1]*clipper_scale)
            });

        } else if (cmd == "L") { /* Line to */
            in >> a[0] >> a[1];
            assert (!in.fail()); /* guaranteed by usvg */

            a = mat.doc2phys(a);
            in_poly.emplace_back(ClipperLib::IntPoint{
                    (ClipperLib::cInt)round(a[0]*clipper_scale),
                    (ClipperLib::cInt)round(a[1]*clipper_scale)
            });

        } else { /* Curve to */
            assert(cmd == "C"); /* guaranteed by usvg */
            in >> b[0] >> b[1]; /* first control point */
            in >> c[0] >> c[1]; /* second control point */
            in >> d[0] >> d[1]; /* end point */
            assert (!in.fail()); /* guaranteed by usvg */

            b = mat.doc2phys(b);
            c = mat.doc2phys(c);
            d = mat.doc2phys(d);

            gerbolyze::curve4_div c4div(distance_tolerance_mm);
            c4div.run(a[0], a[1], b[0], b[1], c[0], c[1], d[0], d[1]);

            for (auto &pt : c4div.points()) {
                in_poly.emplace_back(ClipperLib::IntPoint{
                        (ClipperLib::cInt)round(pt[0]*clipper_scale),
                        (ClipperLib::cInt)round(pt[1]*clipper_scale)
                });
            }

            a = d; /* set last point to curve end point */
        }

        first = false;
    }

    if (!in_poly.empty()) {
        c_stroke.AddPath(in_poly, ClipperLib::ptSubject, false);
        c_fill.AddPath(in_poly, ClipperLib::ptSubject, true);
        num_subpaths += 1;
    }

    return {has_closed, num_subpaths > 1};
}

void gerbolyze::load_svg_path(xform2d &mat, const pugi::xml_node &node, ClipperLib::PolyTree &ptree_stroke, ClipperLib::PolyTree &ptree_fill, double curve_tolerance) {
    auto *path_data = node.attribute("d").value();
    auto fill_rule = clipper_fill_rule(node);

    /* For open paths, clipper does not correctly remove self-intersections. Thus, we pass everything into
     * clipper twice: Once with all paths set to "closed" to compute fill areas, and once with correct
     * open/closed properties for stroke offsetting. */
    ClipperLib::Clipper c_stroke;
    ClipperLib::Clipper c_fill;
    c_stroke.StrictlySimple(true);
    c_fill.StrictlySimple(true);
    auto res = flatten_path(mat, c_stroke, c_fill, path_data, curve_tolerance);
    bool has_closed = res.first, has_multiple = res.second;

    if (!has_closed && !has_multiple) {
        /* FIXME: Workaround!
         *
         * When we render silkscreen layers from gerbv's output, we get a lot of two-point paths (lines). Many of these are
         * horizontal. Now, clipper seems to have a bug (probably related to its scan-line algorithm) that makes it
         * misbehave here:
         *
         * It seems that when the input paths are all perfectly colinear and horizontal, so that the resulting bounding box
         * has zero height, clipper doesn't output anything. At least for open input paths.
         *
         * Since there is no way to get paths out of a Clipper once they're Add'ed, we work around this by just doing an
         * intersection with a maximum-size rectangle instead, that seems to work.
         *
         * TODO: Fix clipper instead.
         */
        auto le_min = -ClipperLib::loRange;
        auto le_max = ClipperLib::hiRange;
        ClipperLib::Path p = {{le_min, le_min}, {le_max, le_min}, {le_max, le_max}, {le_min, le_max}};

        c_stroke.AddPath(p, ClipperLib::ptClip, /* closed= */ true);
        c_stroke.Execute(ClipperLib::ctIntersection, ptree_stroke, fill_rule, ClipperLib::pftNonZero);

        c_fill.AddPath(p, ClipperLib::ptClip, /* closed= */ true);
        c_fill.Execute(ClipperLib::ctIntersection, ptree_fill, fill_rule, ClipperLib::pftNonZero);

    } else {
        /* We cannot clip the polygon here since that would produce incorrect results for our stroke. */
        c_stroke.Execute(ClipperLib::ctUnion, ptree_stroke, fill_rule, ClipperLib::pftNonZero);
        c_fill.Execute(ClipperLib::ctUnion, ptree_fill, fill_rule, ClipperLib::pftNonZero);
    }
}

void gerbolyze::parse_dasharray(const pugi::xml_node &node, vector<double> &out) {
    out.clear();

    string val(node.attribute("stroke-dasharray").value());
    if (val.empty() || val == "none")
        return;

    istringstream desc_stream(val);
    while (!desc_stream.eof()) {
        /* usvg says the array only contains unitless (px) values. I don't know what resvg does with percentages inside
         * dash arrays. We just assume everything is a unitless number here. In case usvg passes through percentages,
         * well, bad luck. They are a kind of weird thing inside a dash array in the first place. */ 
        double d;
        desc_stream >> d;
        out.push_back(d);
    }

    assert(out.size() % 2 == 0); /* according to resvg spec */
}

/* Take a Clipper path in clipper-scaled document units, and apply the given SVG dash array to it. Do this by walking
 * the path from start to end while emitting dashes. */
void gerbolyze::dash_path(const ClipperLib::Path &in, ClipperLib::Paths &out, const vector<double> dasharray, double dash_offset) {
    out.clear();
    if (dasharray.empty() || in.size() < 2) {
        out.push_back(in);
        return;
    }

    size_t dash_idx = 0;
    size_t num_dashes = dasharray.size();
    while (dash_offset > dasharray[dash_idx]) {
        dash_offset -= dasharray[dash_idx];
        dash_idx = (dash_idx + 1) % num_dashes;
    }

    double dash_remaining = dasharray[dash_idx] - dash_offset;

    ClipperLib::Path current_dash;
    current_dash.push_back(in[0]);
    double dbg_total_len = 0.0;
    for (size_t i=1; i<in.size(); i++) {
        ClipperLib::IntPoint p1(in[i-1]), p2(in[i]);

        double x1 = p1.X / clipper_scale, y1 = p1.Y / clipper_scale, x2 = p2.X / clipper_scale, y2 = p2.Y / clipper_scale;
        double dist = sqrt(pow(x2-x1, 2) + pow(y2-y1, 2));
        dbg_total_len += dist;

        if (dist < dash_remaining) {
            /* dash extends beyond this segment, append this segment and continue. */
            dash_remaining -= dist;
            current_dash.push_back(p2);

        } else {
            /* dash started in some previous segment ends in this segment */
            double dash_frac = dash_remaining/dist;
            double x = x1 + (x2 - x1) * dash_frac,
                   y = y1 + (y2 - y1) * dash_frac;
            ClipperLib::IntPoint intermediate {(ClipperLib::cInt)round(x * clipper_scale), (ClipperLib::cInt)round(y * clipper_scale)};

            /* end this dash */
            current_dash.push_back(intermediate);
            if (dash_idx%2 == 0) { /* dash */
                out.push_back(current_dash);
            } /* else space */
            dash_idx = (dash_idx + 1) % num_dashes;
            double offset = dash_remaining;

            /* start next dash */
            current_dash.clear();
            current_dash.push_back(intermediate);

            /* handle case where multiple dashes fit into this segment */
            while ((dist - offset) > dasharray[dash_idx]) {
                offset += dasharray[dash_idx];

                double dash_frac = offset/dist;
                double x = x1 + (x2 - x1) * dash_frac,
                       y = y1 + (y2 - y1) * dash_frac;
                ClipperLib::IntPoint intermediate {(ClipperLib::cInt)round(x * clipper_scale), (ClipperLib::cInt)round(y * clipper_scale)};

                /* end this dash */
                current_dash.push_back(intermediate);
                if (dash_idx%2 == 0) { /* dash */
                    out.push_back(current_dash);
                } /* else space */
                dash_idx = (dash_idx + 1) % num_dashes;

                /* start next dash */
                current_dash.clear();
                current_dash.push_back(intermediate);
            }

            dash_remaining = dasharray[dash_idx] - (dist - offset);
            current_dash.push_back(p2);
        }
    }
}

