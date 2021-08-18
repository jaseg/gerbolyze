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

#include <gerbolyze.hpp>
#include "svg_import_defs.h"
#include "svg_color.h"
#include "svg_geom.h"
#include "svg_path.h"
#include "vec_core.h"

using namespace gerbolyze;
using namespace std;
using namespace ClipperLib;

bool gerbolyze::SVGDocument::load(string filename) {
    ifstream in_f;
    in_f.open(filename);

    return in_f && load(in_f);
}

bool gerbolyze::SVGDocument::load(istream &in) {
    /* Load XML document */
    auto res = svg_doc.load(in);
    if (!res) {
        cerr << "Cannot parse input file" << endl;
        return false;
    }

    root_elem = svg_doc.child("svg");
    if (!root_elem) {
        cerr << "Input file is missing root <svg> element" << endl;
        return false;
    }

    /* Set up the document's viewport transform */
    istringstream vb_stream(root_elem.attribute("viewBox").value());
    vb_stream >> vb_x >> vb_y >> vb_w >> vb_h;
    cerr << "loaded viewbox: " << vb_x << ", " << vb_y << ", " << vb_w << ", " << vb_h << endl;

    page_w = usvg_double_attr(root_elem, "width");
    page_h = usvg_double_attr(root_elem, "height");
    /* usvg resolves all units, but instead of outputting some reasonable absolute length like mm, it converts
     * everything to px, which depends on usvg's DPI setting (--dpi).
     */
    page_w_mm = page_w / assumed_usvg_dpi * 25.4;
    page_h_mm = page_h / assumed_usvg_dpi * 25.4;
    if (!(page_w_mm > 0.0 && page_h_mm > 0.0 && page_w_mm < 10e3 && page_h_mm < 10e3)) {
        cerr << "Warning: Page has zero or negative size, or is larger than 10 x 10 meters! Parsed size: " << page_w << " x " << page_h << " millimeter" << endl;
    }

    if (fabs((vb_w / page_w) / (vb_h / page_h) - 1.0) > 0.001) {
        cerr << "Warning: Document has different document unit scale in x and y direction! Output will likely be garbage!" << endl;
    }

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
    cerr << "match id=" << id << " toplevel=" << is_toplevel << " parent=" << parent_include << endl;
    if (is_toplevel && layers) {
        bool layer_match = std::find(layers->begin(), layers->end(), id) != layers->end();
        if (!layer_match) {
            cerr << "Rejecting layer \"" << id << "\"" << endl;
            return false;
        }
    }

    if (include.empty() && exclude.empty())
        return true;

    bool include_match = std::find(include.begin(), include.end(), id) != include.end();
    bool exclude_match = std::find(exclude.begin(), exclude.end(), id) != exclude.end();
    cerr << "  excl=" << exclude_match << " incl=" << include_match << endl;

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
        ctx.mat().transform_paths(clip_path);
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
                cerr << "Forwarding layer name to sink: \"" << node.attribute("id").value() << "\"" << endl;
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
                cerr << "Cannot resolve vectorizer for node \"" << node.attribute("id").value() << "\"" << endl;
                continue;
            }

            double min_feature_size_px = mm_to_doc_units(ctx.settings().m_minimum_feature_size_mm);
            vec->vectorize_image(elem_ctx, node, min_feature_size_px);
            delete vec;

        } else if (name == "defs") {
            /* ignore */
        } else {
            cerr << "  Unexpected child: <" << node.name() << ">" << endl;
        }
    }
}

/* Export an SVG path element to gerber. Apply patterns and clip on the fly. */
void gerbolyze::SVGDocument::export_svg_path(RenderContext &ctx, const pugi::xml_node &node) {
    enum gerber_color fill_color = gerber_fill_color(node, ctx.settings());
    enum gerber_color stroke_color = gerber_stroke_color(node, ctx.settings());

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

    /* Load path from SVG path data and transform into document units. */
    /* FIXME transform stroke width here? */
    stroke_width = ctx.mat().doc2phys_dist(stroke_width);

    Paths stroke_open, stroke_closed;
    PolyTree ptree_fill;
    PolyTree ptree;
    load_svg_path(ctx.mat(), node, stroke_open, stroke_closed, ptree_fill, ctx.settings().curve_tolerance_mm);

    Paths fill_paths;
    PolyTreeToPaths(ptree_fill, fill_paths);

    bool has_fill = fill_color;
    bool has_stroke = stroke_color && stroke_width > 0.0;

    /* Skip filling for transparent fills. In outline mode, skip filling if a stroke is also set to avoid double lines.
     */
    if (has_fill && !(ctx.settings().outline_mode && has_stroke)) {
        /* Clip paths. Consider all paths closed for filling. */
        if (!ctx.clip().empty()) {
            Clipper c;
            c.AddPaths(fill_paths, ptSubject, /* closed */ true);
            c.AddPaths(ctx.clip(), ptClip, /* closed */ true);
            c.StrictlySimple(true);

            /* fill rules are nonzero since both subject and clip have already been normalized by clipper. */ 
            c.Execute(ctIntersection, ptree_fill, pftNonZero, pftNonZero);
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

                ctx.sink() << (fill_color == GRB_DARK ? GRB_POL_DARK : GRB_POL_CLEAR) << out;
            }
        }
    }

    if (has_stroke) {
        ClipperOffset offx;
        offx.ArcTolerance = 0.01 * clipper_scale; /* 10µm; TODO: Make this configurable */
        offx.MiterLimit = stroke_miterlimit;

        /* For stroking we have to separately handle open and closed paths */
        for (auto &poly : stroke_closed) {
            if (poly.empty())
                continue;

            /* Special case: A closed path becomes a number of open paths when it is dashed. */
            if (dasharray.empty()) {

                if (ctx.settings().outline_mode && stroke_color != GRB_PATTERN_FILL) {
                    /* In outline mode, manually close polys */
                    poly.push_back(poly[0]);
                    ctx.sink() << ApertureToken() << poly;

                } else {
                    offx.AddPath(poly, join_type, etClosedLine);
                }

            } else {
                Path poly_copy(poly);
                poly_copy.push_back(poly[0]);
                Paths out;
                dash_path(poly_copy, out, dasharray, stroke_dashoffset);

                if (ctx.settings().outline_mode && stroke_color != GRB_PATTERN_FILL) {
                    ctx.sink() << ApertureToken(stroke_width) << out;
                } else {
                    offx.AddPaths(out, join_type, end_type);
                }
            }
        }

        for (const auto &poly : stroke_open) {
            Paths out;
            dash_path(poly, out, dasharray, stroke_dashoffset);

            if (ctx.settings().outline_mode && stroke_color != GRB_PATTERN_FILL) {
                ctx.sink() << ApertureToken(stroke_width) << out;
            } else {
                offx.AddPaths(out, join_type, end_type);
            }
        }

        /* Execute clipper offset operation to generate stroke outlines */
        offx.Execute(ptree, 0.5 * stroke_width * clipper_scale);

        /* Clip. Note that after the outline, all we have is closed paths as any open path's stroke outline is itself
         * a closed path. */
        if (!ctx.clip().empty()) {
            Clipper c;

            Paths outline_paths;
            PolyTreeToPaths(ptree, outline_paths);
            c.AddPaths(outline_paths, ptSubject, /* closed */ true);
            c.AddPaths(ctx.clip(), ptClip, /* closed */ true);
            c.StrictlySimple(true);
            /* fill rules are nonzero since both subject and clip have already been normalized by clipper. */ 
            c.Execute(ctIntersection, ptree, pftNonZero, pftNonZero);
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
                RenderContext local_ctx(ctx, xform2d(), clip, true);
                pattern->tile(local_ctx);
            }

        } else if (!ctx.settings().outline_mode) {
            Paths s_polys;
            dehole_polytree(ptree, s_polys);

            ctx.sink() << ApertureToken() << (stroke_color == GRB_DARK ? GRB_POL_DARK : GRB_POL_CLEAR) << s_polys;
        }
    }
    ctx.sink() << ApertureToken();
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

            load_svg_path(child_xf, child, _stroke_open, _stroke_closed, ptree_fill, rset.curve_tolerance_mm);

            Paths paths;
            PolyTreeToPaths(ptree_fill, paths);
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

