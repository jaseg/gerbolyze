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

#include <iostream>
#include <fstream>
#include <cmath>
#include <numbers>

#include <gerbolyze.hpp>
#include "svg_import_defs.h"
#include "svg_color.h"
#include "svg_geom.h"
#include "svg_path.h"
#include "vec_core.h"
#include "nopencv.hpp"

using namespace gerbolyze;
using namespace std;
using namespace ClipperLib;

bool gerbolyze::SVGDocument::load(string filename, double scale) {
    ifstream in_f;
    in_f.open(filename);

    return in_f && load(in_f, scale);
}

bool gerbolyze::SVGDocument::load(istream &in, double scale) {
    /* Load XML document */
    auto res = svg_doc.load(in);
    if (!res) {
        cerr << "Error: Cannot parse input file" << endl;
        return false;
    }

    root_elem = svg_doc.child("svg");
    if (!root_elem) {
        cerr << "Error: Input file is missing root <svg> element" << endl;
        return false;
    }

    page_w = usvg_double_attr(root_elem, "width", std::nan(""));
    page_h = usvg_double_attr(root_elem, "height", std::nan(""));

    /* Set up the document's viewport transform */
    istringstream vb_stream(root_elem.attribute("viewBox").value());
    vb_stream >> vb_x >> vb_y >> vb_w >> vb_h;
    if (vb_stream.eof() || vb_stream.fail()) {
        if (root_elem.attribute("viewBox")) { /* A document with just width/height and no viewBox is okay. */
            cerr << "Warning: Invalid viewBox, defaulting to width/height values" << endl;
        }

        if (isnan(page_w) || isnan(page_h)) {
            cerr << "Warning: Neither width/height nor viewBox given on <svg> root element. Guessing document scale and size." << endl;
            vb_w = vb_h = page_w = page_h = 200000 / 25.4 * assumed_usvg_dpi / scale;
            vb_x = vb_y = -vb_w/2;
        } else {
            cerr << "No viewBox given on <svg> root, using width/height attributes." << endl;
            vb_x = vb_y = 0;
            vb_w = page_w;
            vb_h = page_h;
        }
    } else if (isnan(page_w) || isnan(page_h)) {
        cerr << "No page width or height given, defaulting to viewBox values units." << endl;
        page_w = vb_w;
        page_h = vb_h;
    }

    /* usvg resolves all units, but instead of outputting some reasonable absolute length like mm, it converts
     * everything to px, which depends on usvg's DPI setting (--dpi).
     */
    page_w_mm = page_w / assumed_usvg_dpi * 25.4 * scale;
    page_h_mm = page_h / assumed_usvg_dpi * 25.4 * scale;
    if (!(page_w_mm > 0.0 && page_h_mm > 0.0 && page_w_mm < 10e3 && page_h_mm < 10e3)) {
        cerr << "Warning: Page has zero or negative size, or is larger than 10 x 10 meters! Parsed size: " << page_w << " x " << page_h << " millimeter" << endl;
    }

    if (fabs((vb_w / page_w) / (vb_h / page_h) - 1.0) > 0.001) {
        cerr << "Warning: Document has different document unit scale in x and y direction! Output will likely be garbage!" << endl;
    }

    cerr << "Resulting page width " << page_w_mm << " mm x " << page_h_mm << " mm" << endl;
    cerr << "Resulting document scale " << fabs(vb_w/page_w) << " x " << fabs(vb_h/page_h) << endl;

    /* Get the one document defs element */
    defs_node = root_elem.child("defs");
    if (!defs_node) {
        cerr << "Warning: Input file is missing <defs> node" << endl;
    }

    setup_viewport_clip();
    load_patterns();

    _valid = true;
    return true;
}

const Paths *gerbolyze::SVGDocument::lookup_clip_path(const pugi::xml_node &node) {
    string id(usvg_id_url(node.attribute("clip-path").value()));
    if (id.empty() || clip_path_map.count(id) == 0) {
        return nullptr;
    }
    return &clip_path_map[id];
}

Pattern *gerbolyze::SVGDocument::lookup_pattern(const string id) {
    if (id.empty() || pattern_map.count(id) == 0) {
        return nullptr;
    }
    return &pattern_map[id];
};

/* Used to convert mm values from configuration such as the minimum feature size into document units. */
double gerbolyze::SVGDocument::mm_to_doc_units(double mm) const {
    return mm * (vb_w / page_w_mm);
}

double gerbolyze::SVGDocument::doc_units_to_mm(double px) const {
    return px / (vb_w / page_w_mm);
}

bool IDElementSelector::match(const pugi::xml_node &node, bool is_toplevel, bool parent_include) const {
    string id = node.attribute("id").value();
    //cerr << "match id=" << id << " toplevel=" << is_toplevel << " parent=" << parent_include << endl;
    if (is_toplevel && layers) {
        bool layer_match = std::find(layers->begin(), layers->end(), id) != layers->end();
        if (!layer_match) {
            //cerr << "Rejecting layer \"" << id << "\"" << endl;
            return false;
        }
    }

    if (include.empty() && exclude.empty())
        return true;

    bool include_match = std::find(include.begin(), include.end(), id) != include.end();
    bool exclude_match = std::find(exclude.begin(), exclude.end(), id) != exclude.end();
    //cerr << "  excl=" << exclude_match << " incl=" << include_match << endl;

    if (is_toplevel) {
        if (!include.empty())
            parent_include = false;
        else
            parent_include = true;
    }

    if (exclude_match) {
        return false;
    }

    if (include_match) {
        return true;
    }

    return parent_include;
}

/* Recursively export all SVG elements in the given group. */
void gerbolyze::SVGDocument::export_svg_group(RenderContext &ctx, const pugi::xml_node &group) {

    /* Fetch clip path from global registry and transform it into document coordinates. */
    Paths clip_path;
    auto *lookup = lookup_clip_path(group);
    if (!lookup) {
        string id(usvg_id_url(group.attribute("clip-path").value()));
        if (!id.empty()) {
            cerr << "Warning: Cannot find clip path with ID \"" << group.attribute("clip-path").value() << "\" for group \"" << group.attribute("id").value() << "\"." << endl;
        }

    } else {
        clip_path = *lookup;
        ctx.mat().doc2phys_clipper(clip_path);
    }

    /* Clip against parent's clip path (both are now in document coordinates) */
    if (!ctx.clip().empty()) {
        if (!clip_path.empty()) {
            Clipper c;
            c.StrictlySimple(true);
            c.AddPaths(ctx.clip(), ptClip, /* closed */ true);
            c.AddPaths(clip_path, ptSubject, /* closed */ true);
            /* Nonzero fill since both input clip paths must already have been preprocessed by clipper. */
            c.Execute(ctIntersection, clip_path, pftNonZero);
        } else {
            clip_path = ctx.clip();
        }
    }

    /* Iterate over the group's children, exporting them one by one. */
    for (const auto &node : group.children()) {
        string name(node.name());
        bool match = ctx.match(node);
        RenderContext elem_ctx(ctx, xform2d(node.attribute("transform").value()), clip_path, match);

        if (name == "g") {
            if (ctx.root()) { /* Treat top-level groups as "layers" like inkscape does. */
                LayerNameToken tok { node.attribute("id").value() };
                elem_ctx.sink() << tok;
            }

            export_svg_group(elem_ctx, node);
            
            if (ctx.root()) {
                LayerNameToken tok {""};
                elem_ctx.sink() << tok;
            }

        } else if (name == "path") {
            if (!match)
                continue;

            export_svg_path(elem_ctx, node);

        } else if (name == "image") {
            if (!match)
                continue;

            ImageVectorizer *vec = ctx.settings().m_vec_sel.select(node);
            if (!vec) {
                cerr << "Warning: Cannot resolve vectorizer for node \"" << node.attribute("id").value() << "\", ignoring." << endl;
                continue;
            }

            double min_feature_size_px = mm_to_doc_units(ctx.settings().m_minimum_feature_size_mm);
            vec->vectorize_image(elem_ctx, node, min_feature_size_px);
            delete vec;

        } else if (name == "defs") {
            /* ignore */
        } else {
            cerr << "Warning: Ignoring unexpected child: <" << node.name() << ">" << endl;
        }
    }
}

/* Export an SVG path element to gerber. Apply patterns and clip on the fly. */
void gerbolyze::SVGDocument::export_svg_path(RenderContext &ctx, const pugi::xml_node &node) {
    /* Important note on the document transform:
     *
     * We have to make sure that we dash & stroke (outline) the path *before* transforming into physical units because
     * the transform may not be uniform, i.e. scale may depend on direction. As an example, imagine you stroke a 10 by
     * 10mm square with an 1mm stroke, but there is a transform that scales by 1 in y-direction, and 2 in x-direction.
     * In the output, the stroke is going to be 2mm wide on the left and right, and 1mm wide on the top/bottom.
     */
    enum gerber_color fill_color = gerber_fill_color(node, ctx.settings());
    enum gerber_color stroke_color = gerber_stroke_color(node, ctx.settings());
    //cerr << "path: resolved colors, stroke=" << stroke_color << ", fill=" << fill_color << endl;

    double stroke_width = usvg_double_attr(node, "stroke-width", /* default */ 1.0);
    assert(stroke_width > 0.0);
    enum ClipperLib::EndType end_type = clipper_end_type(node);
    enum ClipperLib::JoinType join_type = clipper_join_type(node);
    vector<double> dasharray;
    parse_dasharray(node, dasharray);
    double stroke_dashoffset = usvg_double_attr(node, "stroke-dashoffset", /* default */ 0.0);
    double stroke_miterlimit = usvg_double_attr(node, "stroke-miterlimit", /* default */ 4.0);

    if (!fill_color && !stroke_color) { /* Ignore "transparent" paths */
        return;
    }

    /* Load path from SVG path data */
    Paths stroke_open, stroke_closed;
    PolyTree ptree_fill;
    PolyTree ptree;
    double geometric_tolerance_px = ctx.mat().phys2doc_min(ctx.settings().geometric_tolerance_mm);
    load_svg_path(node, stroke_open, stroke_closed, ptree_fill, geometric_tolerance_px);

    Paths fill_paths;
    PolyTreeToPaths(ptree_fill, fill_paths);
    /* Since we do not need to stroke them, transform the fill paths to physical units now. For polsby-popper to work
     * properly, they need to be transformed already. However, we leave the stroke paths un-transformed since they can
     * only be transformed after outlining. */ 
    ctx.mat().doc2phys_clipper(fill_paths);

    bool has_fill = fill_color;
    bool has_stroke = stroke_color && ctx.mat().doc2phys_min(stroke_width) > ctx.settings().stroke_width_cutoff;

    //cerr << "processing svg path" << endl;
    //cerr << "  * " << (has_stroke ? "has stroke" : "no stroke") << " / " << (has_fill ? "has fill" : "no fill") << endl;
    //cerr << "  * " << fill_paths.size() << " fill paths" << endl;
    //cerr << "  * " << stroke_closed.size() << " closed strokes" << endl;
    //cerr << "  * " << stroke_open.size() << " open strokes" << endl;

    /* In outline mode, identify drills before applying clip */
    if (ctx.settings().outline_mode && has_fill && fill_color != GRB_PATTERN_FILL) {
        /* Polsby-Popper test */
        for (auto &p : fill_paths) {
            Polygon_i geom_poly(p.size());
            for (size_t i=0; i<p.size(); i++) {
                geom_poly[i] = { p[i].X, p[i].Y };
            }

            double area = nopencv::polygon_area(geom_poly);
            double polsby_popper = 4*std::numbers::pi * area / pow(nopencv::polygon_perimeter(geom_poly), 2);
            polsby_popper = fabs(fabs(polsby_popper) - 1.0);
            if (polsby_popper < ctx.settings().drill_test_polsby_popper_tolerance) {
                if (!ctx.clip().empty()) {
                    Clipper c;
                    c.AddPath(p, ptSubject, /* closed */ true);
                    c.AddPaths(ctx.clip(), ptClip, /* closed */ true);
                    c.StrictlySimple(true);
                    c.Execute(ctDifference, ptree_fill, pftNonZero, pftNonZero);
                    if (ptree_fill.Total() > 0)
                        continue;
                }

                d2p centroid = nopencv::polygon_centroid(geom_poly);
                centroid[0] /= clipper_scale;
                centroid[1] /= clipper_scale;
                
                /* area of n-gon with circumradius 1 relative to circle with radius 1 */
                //double ngon_area_relative = p.size()/(2*std::numbers::pi) * sin(2*std::numbers::pi / p.size());
                // ^- correction not necessary, we already do a very good job.
                double diameter = sqrt(4*fabs(area)/std::numbers::pi) / clipper_scale;
                double tolerance = mm_to_doc_units(ctx.settings().geometric_tolerance_mm);
                diameter = round(diameter/tolerance) * tolerance;
                ctx.sink() << ApertureToken(diameter) << FlashToken(centroid);
            }
        }
        return;
    }

    /* Skip filling for transparent fills. In outline mode, skip filling if a stroke is also set to avoid double lines.
     */
    if (has_fill && !(ctx.settings().outline_mode && has_stroke)) {
        /* Clip paths. Consider all paths closed for filling. */
        if (!ctx.clip().empty()) {
            Clipper c;
            c.AddPaths(fill_paths, ptSubject, /* closed */ true);
            c.AddPaths(ctx.clip(), ptClip, /* closed */ true);
            c.StrictlySimple(true);

            //cerr << "clipping " << fill_paths.size() << " paths, got polytree with " << ptree_fill.ChildCount() << " top-level children" << endl;
            /* fill rules are nonzero since both subject and clip have already been normalized by clipper. */ 
            c.Execute(ctIntersection, ptree_fill, pftNonZero, pftNonZero);
            //cerr << "  > " << ptree_fill.ChildCount() << " clipped fill ptree top-level children" << endl;
        }

        /* Call out to pattern tiler for pattern fills. The path becomes the clip here. */
        if (fill_color == GRB_PATTERN_FILL) {
            string fill_pattern_id = usvg_id_url(node.attribute("fill").value());
            Pattern *pattern = lookup_pattern(fill_pattern_id);
            if (!pattern) {
                cerr << "Warning: Fill pattern with id \"" << fill_pattern_id << "\" not found." << endl;

            } else {
                PolyTreeToPaths(ptree_fill, fill_paths);
                RenderContext local_ctx(ctx, xform2d(), fill_paths, true);
                pattern->tile(local_ctx);
            }

        } else { /* solid fill */
            if (ctx.settings().outline_mode) {
                fill_color = GRB_DARK;
            }

            Paths f_polys;
            /* Important for gerber spec compliance and also for reliable rendering results irrespective of board house
             * and gerber viewer. */
            dehole_polytree(ptree_fill, f_polys);

            /* export gerber */
            for (const auto &poly : f_polys) {
                vector<array<double, 2>> out;
                for (const auto &p : poly)
                    out.push_back(std::array<double, 2>{
                            ((double)p.X) / clipper_scale, ((double)p.Y) / clipper_scale
                            });

                /* In outline mode, manually close polys */
                if (ctx.settings().outline_mode && !out.empty())
                    out.push_back(out[0]);

                ctx.sink() << (fill_color == GRB_DARK ? GRB_POL_DARK : GRB_POL_CLEAR) << ApertureToken() << out;
            }
        }
    }

    if (has_stroke) {
        Clipper stroke_clip;
        stroke_clip.StrictlySimple(true);
        stroke_clip.AddPaths(ctx.clip(), ptClip, /* closed */ true);

        /* We forward strokes as regular gerber interpolations instead of tracing their outline using clipper when one
         * of these is true:
         *
         *  (1) Global outline mode is enabled (-o gerber-outline).
         *  (2) The stroke has round joins and ends and no clip is set.
         * 
         * We have to ignore patterned strokes since then we recursively call down to the pattern renderer. The checks
         * in (2) are to make sure that the semantics of our source SVG align with gerber's aperture semantics. Gerber
         * cannot express anything other than "round" joins and ends. If any part of the path is clipped, the clipped
         * line ends would not be round so we have to exclude that as well. In case of outline mode, we accept this
         * inaccuracies for usability (the only alternative would be to halt and catch fire).
         */
        
        /* Calculate out dashes: A closed path becomes a number of open paths when it is dashed. */
        if (!dasharray.empty()) {
            auto open_copy(stroke_open);
            stroke_open.clear();

            /* FIXME do we handle really really long dashes correctly? */
            for (auto &poly : stroke_closed) {
                poly.push_back(poly[0]);
                dash_path(poly, stroke_open, dasharray, stroke_dashoffset);
            }

            stroke_closed.clear();

            for (auto &poly : open_copy) {
                dash_path(poly, stroke_open, dasharray, stroke_dashoffset);
            }
        }

        if (stroke_color != GRB_PATTERN_FILL
                && ctx.sink().can_do_apertures()
                && ctx.settings().do_gerber_interpolation
                /* check if we have an uniform transform */
                && ctx.mat().doc2phys_skew_ok(stroke_width, 0.05, ctx.settings().geometric_tolerance_mm)) {
            // cerr << "Analyzing direct conversion of stroke" << endl;
            // cerr << "  stroke_closed.size() = " << stroke_closed.size() << endl;
            // cerr << "  stroke_open.size() = " << stroke_open.size() << endl;
            ctx.sink() << (stroke_color == GRB_DARK ? GRB_POL_DARK : GRB_POL_CLEAR);

            ClipperOffset offx;
            offx.ArcTolerance = 0.01 * clipper_scale; /* see below. */
            offx.MiterLimit = 10;
            offx.AddPaths(ctx.clip(), jtRound, etClosedPolygon);
            PolyTree clip_ptree;
            offx.Execute(clip_ptree, -0.5 * ctx.mat().doc2phys_dist(stroke_width) * clipper_scale);

            Paths dilated_clip;
            ClosedPathsFromPolyTree(clip_ptree, dilated_clip);

            Paths stroke_open_phys(stroke_open), stroke_closed_phys(stroke_closed);
            ctx.mat().doc2phys_clipper(stroke_open_phys);
            ctx.mat().doc2phys_clipper(stroke_closed_phys);

            Clipper stroke_clip;
            stroke_clip.StrictlySimple(true);
            stroke_clip.AddPaths(dilated_clip, ptClip, /* closed */ true);
            stroke_clip.AddPaths(stroke_closed_phys, ptSubject, /* closed */ true);
            stroke_clip.AddPaths(stroke_open_phys, ptSubject, /* closed */ false);
            stroke_clip.Execute(ctDifference, ptree, pftNonZero, pftNonZero);
            // cerr << "  > " << ptree.ChildCount() << " clipped stroke ptree top-level children" << endl;
            
            /* Did any part of the path clip the clip path (which defaults to the document border)? */
            bool nothing_clipped = ptree.Total() == 0;

            /* Can all joins be mapped? True if either jtRound, or if there are no joins. */
            bool joins_can_be_mapped = true;
            if (join_type != ClipperLib::jtRound) {
                for (auto &p : stroke_closed) {
                    if (p.size() > 2) {
                        joins_can_be_mapped = false;
                    }
                }
            }

            /* Can all ends be mapped? True if either etOpenRound or if there are no ends (we only have closed paths) */
            bool ends_can_be_mapped = (end_type == ClipperLib::etOpenRound) || (stroke_open.size() == 0);
            /* Can gerber losslessly express this path? */
            bool gerber_lossless = nothing_clipped && ends_can_be_mapped && joins_can_be_mapped;
            //cerr << "  ends_can_be_mapped=" << ends_can_be_mapped << ", nothing_clipped=" << nothing_clipped << ", joins_can_be_mapped=" << joins_can_be_mapped << endl;
            
            // cerr << "  nothing_clipped = " << nothing_clipped << endl;
            // cerr << "  ends_can_be_mapped = " << ends_can_be_mapped << endl;
            // cerr << "  joins_can_be_mapped = " << joins_can_be_mapped << endl;
            /* Accept loss of precision in outline mode. */
            if (ctx.settings().outline_mode || gerber_lossless) {
                //cerr << "  -> converting directly" << endl;
                ctx.mat().doc2phys_clipper(stroke_closed);
                ctx.mat().doc2phys_clipper(stroke_open);

                ctx.sink() << ApertureToken(ctx.mat().doc2phys_dist(stroke_width));
                for (auto &path : stroke_closed) {
                    if (path.empty()) {
                        continue;
                    }
                    /* We have to manually close these here. */
                    path.push_back(path[0]);
                    ctx.sink() << path;
                }
                ctx.sink() << stroke_open;
                return;
            }
            //cerr << "  -> NOT converting directly" << endl;
            /* else fall through to normal processing */
        }

        ClipperOffset offx;
        offx.ArcTolerance = ctx.mat().phys2doc_min(ctx.settings().geometric_tolerance_mm) * clipper_scale;
        offx.MiterLimit = stroke_miterlimit;

        //cerr << "  offsetting " << stroke_closed.size() << " closed and " << stroke_open.size() << " open paths" << endl;
        //cerr << "  geometric tolerance = " << ctx.settings().geometric_tolerance_mm << " mm" << endl;
        //cerr << "  arc tolerance = " << offx.ArcTolerance/clipper_scale << " px" << endl;
        //cerr << "  stroke_width=" << stroke_width << "px" << endl;
        //cerr << "  offset = " << (0.5 * stroke_width * clipper_scale) << endl;

        /* For stroking we have to separately handle open and closed paths since coincident start and end points may
         * render differently than joined start and end points. */
        offx.AddPaths(stroke_closed, join_type, etClosedLine);
        offx.AddPaths(stroke_open, join_type, end_type);
        /* Execute clipper offset operation to generate stroke outlines */
        offx.Execute(ptree, 0.5 * stroke_width * clipper_scale);

        /* Clip. Note that (outside of outline mode) after the clipper outline operation, all we have is closed paths as
         * any open path's stroke outline is itself a closed path. */
        if (!ctx.clip().empty()) {
            //cerr << "  Clipping polytree" << endl;
            Paths outline_paths;
            PolyTreeToPaths(ptree, outline_paths);

            Paths clip(ctx.clip());
            ctx.mat().phys2doc_clipper(clip);

            Clipper stroke_clip;
            stroke_clip.StrictlySimple(true);
            stroke_clip.AddPaths(clip, ptClip, /* closed */ true);
            stroke_clip.AddPaths(outline_paths, ptSubject, /* closed */ true);
            /* fill rules are nonzero since both subject and clip have already been normalized by clipper. */ 
            stroke_clip.Execute(ctIntersection, ptree, pftNonZero, pftNonZero);
        }

        /* Call out to pattern tiler for pattern strokes. The stroke's outline becomes the clip here. */
        if (stroke_color == GRB_PATTERN_FILL) {
            string stroke_pattern_id = usvg_id_url(node.attribute("stroke").value());
            Pattern *pattern = lookup_pattern(stroke_pattern_id);
            if (!pattern) {
                cerr << "Warning: Fill pattern with id \"" << stroke_pattern_id << "\" not found." << endl;

            } else {
                Paths clip;
                PolyTreeToPaths(ptree, clip);
                ctx.mat().phys2doc_clipper(clip);

                RenderContext local_ctx(ctx, xform2d(), clip, true);
                pattern->tile(local_ctx);
            }

        } else {
            Paths s_polys;
            dehole_polytree(ptree, s_polys);
            ctx.mat().doc2phys_clipper(s_polys);
            /* color has already been pushed above. */
            //cerr << "  sinking " << s_polys.size() << " paths" << endl;
            ctx.sink() << (stroke_color == GRB_DARK ? GRB_POL_DARK : GRB_POL_CLEAR) << ApertureToken() << s_polys;
        }
    }
}

void gerbolyze::SVGDocument::render(const RenderSettings &rset, PolygonSink &sink, const ElementSelector &sel) {
    assert(_valid);
    /* Export the actual SVG document. We do this as we go, i.e. we immediately process each element to gerber as we
     * encounter it instead of first rendering everything to a giant list of gerber primitives and then serializing
     * those later. Exporting them on the fly saves a ton of memory and is much faster.
     */

    /* Scale document pixels to mm for sinks */
    PolygonScaler scaler(sink, doc_units_to_mm(1.0));
    RenderContext ctx(rset, scaler, sel, vb_paths);

    /* Load clip paths from defs with given bezier flattening tolerance and unit scale */
    load_clips(rset);

    scaler.header({vb_x, vb_y}, {vb_w, vb_h});
    export_svg_group(ctx, root_elem);
    scaler.footer();
}

void gerbolyze::SVGDocument::render_to_list(const RenderSettings &rset, vector<pair<Polygon, GerberPolarityToken>> &out, const ElementSelector &sel) {
    LambdaPolygonSink sink([&out](const Polygon &poly, GerberPolarityToken pol) {
            out.emplace_back(pair<Polygon, GerberPolarityToken>{poly, pol});
        });
    render(rset, sink, sel);
}

void gerbolyze::SVGDocument::setup_viewport_clip() {
    /* Set up view port clip path */
    Path vb_path;
    //cerr << "setting up viewport clip at " << vb_x << ", " << vb_y << " with size " << vb_w << ", " << vb_h << endl;
    for (d2p &p : vector<d2p> {
            {vb_x,      vb_y},
            {vb_x+vb_w, vb_y},
            {vb_x+vb_w, vb_y+vb_h},
            {vb_x,      vb_y+vb_h}}) {
        vb_path.push_back({ (cInt)round(p[0] * clipper_scale), (cInt)round(p[1] * clipper_scale) });
    }
    vb_paths.push_back(vb_path);
}

void gerbolyze::SVGDocument::load_patterns() {
    /* Set up document-wide pattern registry. Load patterns from <defs> node. */
    for (const auto &node : defs_node.children("pattern")) {
        pattern_map.emplace(std::piecewise_construct, std::forward_as_tuple(node.attribute("id").value()), std::forward_as_tuple(node, *this));
    }
}

void gerbolyze::SVGDocument::load_clips(const RenderSettings &rset) {
    /* Set up document-wide clip path registry: Extract clip path definitions from <defs> element */
    for (const auto &node : defs_node.children("clipPath")) {

        xform2d local_xf(node.attribute("transform").value());

        string meta_clip_path_id(usvg_id_url(node.attribute("clip-path").value()));
        Clipper c;

        /* The clipPath node can only contain <path> children. usvg converts all geometric objects (rect etc.) to
         * <path>s. Raster images are invalid inside a clip path. usvg removes all groups that are not relevant to
         * rendering, and the only way a group might stay is if it affects rasterization (e.g. through mask, clipPath).
         */
        for (const auto &child : node.children("path")) {
            Paths _stroke_open, _stroke_closed; /* discarded */
            PolyTree ptree_fill;
            /* TODO: we currently only support clipPathUnits="userSpaceOnUse", not "objectBoundingBox". */
            xform2d child_xf(local_xf);
            child_xf.transform(xform2d(child.attribute("transform").value()));

            load_svg_path(child, _stroke_open, _stroke_closed, ptree_fill, rset.geometric_tolerance_mm);

            Paths paths;
            PolyTreeToPaths(ptree_fill, paths);
            child_xf.doc2phys_clipper(paths);
            c.AddPaths(paths, ptSubject, /* closed */ false);
        }

        /* Support clip paths that themselves have clip paths */
        if (!meta_clip_path_id.empty()) {
            if (clip_path_map.count(meta_clip_path_id) > 0) {
                /* all clip paths must be closed */
                c.AddPaths(clip_path_map[meta_clip_path_id], ptClip, /* closed */ true);

            } else {
                cerr << "Warning: Cannot find clip path with ID \"" << meta_clip_path_id << "\", ignoring." << endl;
            }
        }

        PolyTree ptree;
        c.StrictlySimple(true);
        /* This unions all child <path>s together and at the same time applies any meta clip path. */
        /* The fill rules are both nonzero since both subject and clip have already been normalized by clipper. */ 
        c.Execute(ctUnion, ptree, pftNonZero, pftNonZero);
        /* Insert into document clip path map */
        PolyTreeToPaths(ptree, clip_path_map[node.attribute("id").value()]);
    }
}


gerbolyze::RenderContext::RenderContext(const RenderSettings &settings,
        PolygonSink &sink,
        const ElementSelector &sel,
        ClipperLib::Paths &clip) :
    m_sink(sink),
    m_settings(settings),
    m_mat(),
    m_root(true),
    m_included(false),
    m_sel(sel),
    m_clip(clip)
{
}

gerbolyze::RenderContext::RenderContext(RenderContext &parent, xform2d transform) :
    RenderContext(parent, transform, parent.clip(), parent.included())
{
}

gerbolyze::RenderContext::RenderContext(RenderContext &parent, xform2d transform, ClipperLib::Paths &clip, bool included) :
    m_sink(parent.sink()),
    m_settings(parent.settings()),
    m_mat(parent.mat()),
    m_root(false),
    m_included(included),
    m_sel(parent.sel()),
    m_clip(clip)
{
    m_mat.transform(transform);
}

gerbolyze::RenderContext::RenderContext(RenderContext &parent, PolygonSink &sink, ClipperLib::Paths &clip) :
    m_sink(sink),
    m_settings(parent.settings()),
    m_mat(parent.mat()),
    m_root(false),
    m_included(true),
    m_sel(parent.sel()),
    m_clip(clip)
{
}

