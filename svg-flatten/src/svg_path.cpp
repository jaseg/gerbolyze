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
#include <sstream>
#include "cairo_clipper.hpp"
#include "svg_import_defs.h"
#include "svg_path.h"

using namespace std;

static void clipper_add_cairo_path(cairo_t *cr, ClipperLib::Clipper &c, bool closed) {
    ClipperLib::Paths in_poly;
    ClipperLib::cairo::cairo_to_clipper(cr, in_poly, CAIRO_PRECISION, ClipperLib::cairo::tNone);
    c.AddPaths(in_poly, ClipperLib::ptSubject, closed);
}

static pair<bool, bool> path_to_clipper_via_cairo(cairo_t *cr, ClipperLib::Clipper &c_stroke, ClipperLib::Clipper &c_fill, const pugi::char_t *path_data) {
    istringstream d(path_data);

    string cmd;
    double x, y, c1x, c1y, c2x, c2y;

    bool first = true;
    bool has_closed = false;
    bool path_is_empty = true;
    int num_subpaths = 0;
    while (!d.eof()) {
        d >> cmd;
        assert (!d.fail());
        assert(!first || cmd == "M");
        
        if (cmd == "Z") { /* Close path */
            cairo_close_path(cr);
            clipper_add_cairo_path(cr, c_stroke, /* closed= */ true);
            clipper_add_cairo_path(cr, c_fill, /* closed= */ true);
            has_closed = true;
            cairo_new_path(cr);
            path_is_empty = true;
            num_subpaths += 1;

        } else if (cmd == "M") { /* Move to */
            if (!first && !path_is_empty) {
                cairo_close_path(cr);
                clipper_add_cairo_path(cr, c_stroke, /* closed= */ false);
                clipper_add_cairo_path(cr, c_fill, /* closed= */ true);
                num_subpaths += 1;
            }

            cairo_new_path (cr);

            d >> x >> y;
            /* We need to transform all points ourselves here, and cannot use the transform feature of cairo_to_clipper:
             * Our transform may contain offsets, and clipper only passes its data into cairo's transform functions
             * after scaling up to its internal fixed-point ints, but it does not scale the transform accordingly. This
             * means a scale/rotation we set before calling clipper works out fine, but translations get lost as they
             * get scaled by something like 1e-6.
             */
            cairo_user_to_device(cr, &x, &y);
            assert (!d.fail());
            path_is_empty = true;
            cairo_move_to(cr, x, y);

        } else if (cmd == "L") { /* Line to */
            d >> x >> y;
            cairo_user_to_device(cr, &x, &y);
            assert (!d.fail());
            cairo_line_to(cr, x, y);
            path_is_empty = false;

        } else { /* Curve to */
            assert(cmd == "C");
            d >> c1x >> c1y; /* first control point */
            cairo_user_to_device(cr, &c1x, &c1y);
            d >> c2x >> c2y; /* second control point */
            cairo_user_to_device(cr, &c2x, &c2y);
            d >> x >> y; /* end point */
            cairo_user_to_device(cr, &x, &y);
            assert (!d.fail());
            cairo_curve_to(cr, c1x, c1y, c2x, c2y, x, y);
            path_is_empty = false;
        }

        first = false;
    }
    if (!path_is_empty) {
        cairo_close_path(cr);
        clipper_add_cairo_path(cr, c_stroke, /* closed= */ false);
        clipper_add_cairo_path(cr, c_fill, /* closed= */ true);
        num_subpaths += 1;
    }

    return {has_closed, num_subpaths > 1};
}

void gerbolyze::load_svg_path(cairo_t *cr, const pugi::xml_node &node, ClipperLib::PolyTree &ptree_stroke, ClipperLib::PolyTree &ptree_fill, double curve_tolerance) {
    auto *path_data = node.attribute("d").value();
    auto fill_rule = clipper_fill_rule(node);

    /* For open paths, clipper does not correctly remove self-intersections. Thus, we pass everything into
     * clipper twice: Once with all paths set to "closed" to compute fill areas, and once with correct
     * open/closed properties for stroke offsetting. */
    cairo_set_tolerance (cr, curve_tolerance); /* FIXME make configurable, scale properly for units */
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);

    ClipperLib::Clipper c_stroke;
    ClipperLib::Clipper c_fill;
    c_stroke.StrictlySimple(true);
    c_fill.StrictlySimple(true);
    auto res = path_to_clipper_via_cairo(cr, c_stroke, c_fill, path_data);
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
        ptree_fill.Clear();

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

