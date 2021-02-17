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

gerbolyze::SVGDocument::~SVGDocument() {
    if (cr)
        cairo_destroy (cr);
    if (surface)
        cairo_surface_destroy (surface);
}

bool gerbolyze::SVGDocument::load(string filename, string debug_out_filename) {
    ifstream in_f;
    in_f.open(filename);

    return in_f && load(in_f, debug_out_filename);
}

bool gerbolyze::SVGDocument::load(istream &in, string debug_out_filename) {
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

    setup_debug_output(debug_out_filename);
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

bool IDElementSelector::match(const pugi::xml_node &node, bool included, bool is_root) const {
    string id = node.attribute("id").value();
    if (is_root && layers) {
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

    if (exclude_match || (!included && !include_match)) {
        return false;
    }

    return true;
}

/* Recursively export all SVG elements in the given group. */
void gerbolyze::SVGDocument::export_svg_group(const RenderSettings &rset, const pugi::xml_node &group, Paths &parent_clip_path, const ElementSelector *sel, bool included, bool is_root) {

    /* Load clip paths from defs given bezier flattening tolerance from rset */
    load_clips(rset);
    
    /* Enter the group's coordinate system */
    cairo_save(cr);
    apply_cairo_transform_from_svg(cr, group.attribute("transform").value());

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
    }
    transform_paths(cr, clip_path);

    /* Clip against parent's clip path (both are now in document coordinates) */
    if (!parent_clip_path.empty()) {
        if (!clip_path.empty()) {
            combine_clip_paths(parent_clip_path, clip_path, clip_path);
        } else {
            clip_path = parent_clip_path;
        }
    }

    /* Iterate over the group's children, exporting them one by one. */
    for (const auto &node : group.children()) {
        if (sel && !sel->match(node, included, is_root))
            continue;

        string name(node.name());
        if (name == "g") {
            if (is_root) { /* Treat top-level groups as "layers" like inkscape does. */
                cerr << "Forwarding layer name to sink: \"" << node.attribute("id").value() << "\"" << endl;
                LayerNameToken tok { node.attribute("id").value() };
                *polygon_sink << tok;
            }

            export_svg_group(rset, node, clip_path, sel, true);
            
            if (is_root) {
                LayerNameToken tok {""};
                *polygon_sink << tok;
            }

        } else if (name == "path") {
            export_svg_path(rset, node, clip_path);

        } else if (name == "image") {
            ImageVectorizer *vec = rset.m_vec_sel.select(node);
            if (!vec) {
                cerr << "Cannot resolve vectorizer for node \"" << node.attribute("id").value() << "\"" << endl;
                continue;
            }

            double min_feature_size_px = mm_to_doc_units(rset.m_minimum_feature_size_mm);
            vec->vectorize_image(cr, node, clip_path, viewport_matrix, *polygon_sink, min_feature_size_px);
            delete vec;

        } else if (name == "defs") {
            /* ignore */
        } else {
            cerr << "  Unexpected child: <" << node.name() << ">" << endl;
        }
    }

    cairo_restore(cr);
}

/* Export an SVG path element to gerber. Apply patterns and clip on the fly. */
void gerbolyze::SVGDocument::export_svg_path(const RenderSettings &rset, const pugi::xml_node &node, Paths &clip_path) {
    enum gerber_color fill_color = gerber_fill_color(node);
    enum gerber_color stroke_color = gerber_stroke_color(node);

    double stroke_width = usvg_double_attr(node, "stroke-width", /* default */ 1.0);
    assert(stroke_width > 0.0);
    enum ClipperLib::EndType end_type = clipper_end_type(node);
    enum ClipperLib::JoinType join_type = clipper_join_type(node);
    vector<double> dasharray;
    parse_dasharray(node, dasharray);
    /* TODO add stroke-miterlimit */

    if (!fill_color && !stroke_color) { /* Ignore "transparent" paths */
        return;
    }

    /* Load path from SVG path data and transform into document units. */
    cairo_save(cr);
    apply_cairo_transform_from_svg(cr, node.attribute("transform").value());

    PolyTree ptree_stroke;
    PolyTree ptree_fill;
    PolyTree ptree;
    load_svg_path(cr, node, ptree_stroke, ptree_fill, rset.curve_tolerance_mm);

    double _y = 0;
    cairo_user_to_device_distance(cr, &stroke_width, &_y);

    cairo_restore (cr);

    Paths open_paths, closed_paths, fill_paths;
    OpenPathsFromPolyTree(ptree_stroke, open_paths);
    ClosedPathsFromPolyTree(ptree_stroke, closed_paths);
    PolyTreeToPaths(ptree_fill, fill_paths);

    /* Skip filling for transparent fills */
    if (fill_color) {
        /* Clip paths. Consider all paths closed for filling. */
        if (!clip_path.empty()) {
            Clipper c;
            c.AddPaths(fill_paths, ptSubject, /* closed */ true);
            c.AddPaths(clip_path, ptClip, /* closed */ true);
            c.StrictlySimple(true);
            /* fill rules are nonzero since both subject and clip have already been normalized by clipper. */ 
            c.Execute(ctIntersection, ptree, pftNonZero, pftNonZero);
            PolyTreeToPaths(ptree, fill_paths);
        }

        /* Call out to pattern tiler for pattern fills. The path becomes the clip here. */
        if (fill_color == GRB_PATTERN_FILL) {
            string fill_pattern_id = usvg_id_url(node.attribute("fill").value());
            Pattern *pattern = lookup_pattern(fill_pattern_id);
            if (!pattern) {
                cerr << "Warning: Fill pattern with id \"" << fill_pattern_id << "\" not found." << endl;

            } else {
                pattern->tile(rset, fill_paths);
            }

        } else { /* solid fill */
            Paths f_polys;
            /* Important for gerber spec compliance and also for reliable rendering results irrespective of board house
             * and gerber viewer. */
            dehole_polytree(ptree_fill, f_polys);

            /* Export SVG */
            cairo_save(cr);
            cairo_set_matrix(cr, &viewport_matrix);
            cairo_new_path(cr);
            ClipperLib::cairo::clipper_to_cairo(f_polys, cr, CAIRO_PRECISION, ClipperLib::cairo::tNone);
            if (fill_color == GRB_DARK) {
                cairo_set_source_rgba(cr, 0.0, 0.0, 1.0, dbg_fill_alpha);
            } else { /* GRB_CLEAR */
                cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, dbg_fill_alpha);
            }
            cairo_fill (cr);

            /* export gerber */
            cairo_identity_matrix(cr);
            for (const auto &poly : f_polys) {
                vector<array<double, 2>> out;
                for (const auto &p : poly)
                    out.push_back(std::array<double, 2>{
                            ((double)p.X) / clipper_scale, ((double)p.Y) / clipper_scale
                            });
                *polygon_sink << (fill_color == GRB_DARK ? GRB_POL_DARK : GRB_POL_CLEAR) << out;
            }
            cairo_restore(cr);
        }
    }

    if (stroke_color && stroke_width > 0.0) {
        ClipperOffset offx;
        offx.ArcTolerance = 0.01 * clipper_scale; /* 10µm; TODO: Make this configurable */

        /* For stroking we have to separately handle open and closed paths */
        for (const auto &poly : closed_paths) {
            if (poly.empty()) /* do we need this? */
                continue;

            /* Special case: A closed path becomes a number of open paths when it is dashed. */
            if (dasharray.empty()) {
                offx.AddPath(poly, join_type, etClosedLine);

            } else {
                Path poly_copy(poly);
                poly_copy.push_back(poly[0]);
                Paths out;
                dash_path(poly_copy, out, dasharray);
                offx.AddPaths(out, join_type, end_type);
            }
        }

        for (const auto &poly : open_paths) {
            Paths out;
            dash_path(poly, out, dasharray);
            offx.AddPaths(out, join_type, end_type);
        }

        /* Execute clipper offset operation to generate stroke outlines */
        offx.Execute(ptree, 0.5 * stroke_width * clipper_scale);

        /* Clip. Note that after the outline, all we have is closed paths as any open path's stroke outline is itself
         * a closed path. */
        if (!clip_path.empty()) {
            Clipper c;

            Paths outline_paths;
            PolyTreeToPaths(ptree, outline_paths);
            c.AddPaths(outline_paths, ptSubject, /* closed */ true);
            c.AddPaths(clip_path, ptClip, /* closed */ true);
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
                pattern->tile(rset, clip);
            }

        } else {
            Paths s_polys;
            dehole_polytree(ptree, s_polys);

            /* Export debug svg */
            cairo_save(cr);
            cairo_set_matrix(cr, &viewport_matrix);
            cairo_new_path(cr);
            ClipperLib::cairo::clipper_to_cairo(s_polys, cr, CAIRO_PRECISION, ClipperLib::cairo::tNone);
            if (stroke_color == GRB_DARK) {
                cairo_set_source_rgba(cr, 0.0, 0.0, 1.0, dbg_stroke_alpha);
            } else { /* GRB_CLEAR */
                cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, dbg_stroke_alpha);
            }
            cairo_fill (cr);

            /* export gerber */
            cairo_identity_matrix(cr);
            for (const auto &poly : s_polys) {
                vector<array<double, 2>> out;
                for (const auto &p : poly)
                    out.push_back(std::array<double, 2>{
                            ((double)p.X) / clipper_scale, ((double)p.Y) / clipper_scale
                            });
                *polygon_sink << (stroke_color == GRB_DARK ? GRB_POL_DARK : GRB_POL_CLEAR) << out;
            }
            cairo_restore(cr);
        }
    }
}

void gerbolyze::SVGDocument::render(const RenderSettings &rset, PolygonSink &sink, const ElementSelector *sel) {
    assert(_valid);
    /* Export the actual SVG document to both SVG for debuggin and to gerber. We do this as we go, i.e. we immediately
     * process each element to gerber as we encounter it instead of first rendering everything to a giant list of gerber
     * primitives and then serializing those later. Exporting them on the fly saves a ton of memory and is much faster.
     */
    polygon_sink = &sink;
    sink.header({vb_x, vb_y}, {vb_w, vb_h});
    ClipperLib::Clipper c;
    c.AddPaths(vb_paths, ptSubject, /* closed */ true);
    ClipperLib::IntRect bbox = c.GetBounds();
    cerr << "document viewbox clip: bbox={" << bbox.left << ", " << bbox.top << "} - {" << bbox.right << ", " << bbox.bottom << "}" << endl;
    export_svg_group(rset, root_elem, vb_paths, sel, false, true);
    sink.footer();
}

void gerbolyze::SVGDocument::render_to_list(const RenderSettings &rset, vector<pair<Polygon, GerberPolarityToken>> &out, const ElementSelector *sel) {
    LambdaPolygonSink sink([&out](const Polygon &poly, GerberPolarityToken pol) {
            out.emplace_back(pair<Polygon, GerberPolarityToken>{poly, pol});
        });
    render(rset, sink, sel);
}

void gerbolyze::SVGDocument::setup_debug_output(string filename) {
    /* Setup cairo to draw into a SVG surface (for debugging). For actual rendering, something like a recording surface
     * would work fine, too. */
    /* Cairo expects the SVG surface size to be given in pt (72.0 pt = 1.0 in = 25.4 mm) */
    const char *fn = filename.empty() ? nullptr : filename.c_str();
    assert (!cr);
    assert (!surface);
    surface = cairo_svg_surface_create(fn, page_w_mm / 25.4 * 72.0, page_h_mm / 25.4 * 72.0);
    cr = cairo_create (surface);
    /* usvg returns "pixels", cairo thinks we draw "points" at 72.0 pt per inch. */
    cairo_scale(cr, page_w / vb_w * 72.0 / assumed_usvg_dpi, page_h / vb_h * 72.0 / assumed_usvg_dpi);

    cairo_translate(cr, -vb_x, -vb_y);

    /* Store viewport transform and reset cairo's active transform. We have to do this since we have to render out all
     * gerber primitives in mm, not px and most gerber primitives we export pass through Cairo at some point.
     *
     * We manually apply this viewport transform every time for debugging we actually use Cairo to export SVG. */
    cairo_get_matrix(cr, &viewport_matrix);
    cairo_identity_matrix(cr);

    cairo_set_line_width (cr, 0.1);
    cairo_set_source_rgba (cr, 1.0, 0.0, 0.0, 1.0);
}

void gerbolyze::SVGDocument::setup_viewport_clip() {
    /* Set up view port clip path */
    Path vb_path;
    for (auto &elem : vector<pair<double, double>> {{vb_x, vb_y}, {vb_x+vb_w, vb_y}, {vb_x+vb_w, vb_y+vb_h}, {vb_x, vb_y+vb_h}}) {
        double x = elem.first, y = elem.second;
        vb_path.push_back({ (cInt)round(x * clipper_scale), (cInt)round(y * clipper_scale) });
    }
    vb_paths.push_back(vb_path);

    ClipperLib::Clipper c;
    c.AddPaths(vb_paths, ptSubject, /* closed */ true);
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
        cairo_save(cr);
        apply_cairo_transform_from_svg(cr, node.attribute("transform").value());

        string meta_clip_path_id(usvg_id_url(node.attribute("clip-path").value()));
        Clipper c;

        /* The clipPath node can only contain <path> children. usvg converts all geometric objects (rect etc.) to
         * <path>s. Raster images are invalid inside a clip path. usvg removes all groups that are not relevant to
         * rendering, and the only way a group might stay is if it affects rasterization (e.g. through mask, clipPath).
         */
        for (const auto &child : node.children("path")) {
            PolyTree ptree_stroke; /* discarded */
            PolyTree ptree_fill;
            cairo_save(cr);
            /* TODO: we currently only support clipPathUnits="userSpaceOnUse", not "objectBoundingBox". */
            apply_cairo_transform_from_svg(cr, child.attribute("transform").value());
            load_svg_path(cr, child, ptree_stroke, ptree_fill, rset.curve_tolerance_mm);
            cairo_restore (cr);

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

        cairo_restore(cr);
    }
}

/* Note: These values come from KiCAD's common/lset.cpp. KiCAD uses *multiple different names* for the same layer in
 * different places, and not all of them are stable. Sometimes, these names change without notice. If this list isn't
 * up-to-date, it's not my fault. Still, please file an issue. */
const std::vector<std::string> gerbolyze::kicad_default_layers ({
        /* Copper */
        "F.Cu",
        "In1.Cu", "In2.Cu", "In3.Cu", "In4.Cu", "In5.Cu", "In6.Cu", "In7.Cu", "In8.Cu",
        "In9.Cu", "In10.Cu", "In11.Cu", "In12.Cu", "In13.Cu", "In14.Cu", "In15.Cu", "In16.Cu",
        "In17.Cu", "In18.Cu", "In19.Cu", "In20.Cu", "In21.Cu", "In22.Cu", "In23.Cu",
        "In24.Cu", "In25.Cu", "In26.Cu", "In27.Cu", "In28.Cu", "In29.Cu", "In30.Cu",
        "B.Cu",

        /* Technical layers */
        "B.Adhes", "F.Adhes",
        "B.Paste", "F.Paste",
        "B.SilkS", "F.SilkS",
        "B.Mask", "F.Mask",

        /* User layers */
        "Dwgs.User",
        "Cmts.User",
        "Eco1.User", "Eco2.User",
        "Edge.Cuts",
        "Margin",

        /* Footprint layers */
        "F.CrtYd", "B.CrtYd",
        "F.Fab", "B.Fab",

        /* Layers for user scripting etc. */
        "User.1", "User.2", "User.3", "User.4", "User.5", "User.6", "User.7", "User.8", "User.9",
        });
