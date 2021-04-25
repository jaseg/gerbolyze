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
#include <string>
#include <iostream>
#include <algorithm>
#include <vector>
#include <regex>
#include <opencv2/opencv.hpp>
#include "svg_import_util.h"
#include "vec_core.h"
#include "svg_import_defs.h"
#include "jc_voronoi.h"

using namespace gerbolyze;
using namespace std;

ImageVectorizer *gerbolyze::makeVectorizer(const std::string &name) {
    if (name == "poisson-disc")
        return new VoronoiVectorizer(POISSON_DISC, /* relax */ true);
    else if (name == "hex-grid")
        return new VoronoiVectorizer(HEXGRID, /* relax */ false);
    else if (name == "square-grid")
        return new VoronoiVectorizer(SQUAREGRID, /* relax */ false);
    else if (name == "binary-contours")
        return new OpenCVContoursVectorizer();
    else if (name == "dev-null")
        return new DevNullVectorizer();

    return nullptr;
}

/* From jcv voronoi README */
static void voronoi_relax_points(const jcv_diagram* diagram, jcv_point* points) {
    const jcv_site* sites = jcv_diagram_get_sites(diagram);
    for (int i=0; i<diagram->numsites; i++) {
        const jcv_site* site = &sites[i];
        jcv_point sum = site->p;
        int count = 1;

        const jcv_graphedge* edge = site->edges;

        while (edge) {
            sum.x += edge->pos[0].x;
            sum.y += edge->pos[0].y;
            count++;
            edge = edge->next;
        }

        points[site->index].x = sum.x / count;
        points[site->index].y = sum.y / count;
    }
} 

void gerbolyze::parse_img_meta(const pugi::xml_node &node, double &x, double &y, double &width, double &height) {
    /* Read XML node attributes */
    x = usvg_double_attr(node, "x", 0.0);
    y = usvg_double_attr(node, "y", 0.0);
    width = usvg_double_attr(node, "width", 0.0);
    height = usvg_double_attr(node, "height", 0.0);
    assert (width > 0 && height > 0);
    cerr << "image elem: w="<<width<<", h="<<height<<endl;
}

string gerbolyze::read_img_data(const pugi::xml_node &node) {
    /* Read image from data:base64... URL */
    string img_data = parse_data_iri(node.attribute("xlink:href").value());
    if (img_data.empty()) {
        cerr << "Warning: Empty or invalid image element with id \"" << node.attribute("id").value() << "\"" << endl;
        return "";
    }
    return img_data;
}

cv::Mat read_img_opencv(const pugi::xml_node &node) {
    string img_data = read_img_data(node);

    /* slightly annoying round-trip through the std:: and cv:: APIs */
    vector<unsigned char> img_vec(img_data.begin(), img_data.end());
    cv::Mat data_mat(img_vec, true);
    cv::Mat img = cv::imdecode(data_mat, cv::ImreadModes::IMREAD_GRAYSCALE | cv::ImreadModes::IMREAD_ANYDEPTH);
    data_mat.release();

    if (img.empty()) {
        cerr << "Warning: Could not decode content of image element with id \"" << node.attribute("id").value() << "\"" << endl;
    }

    return img;
}

void gerbolyze::draw_bg_rect(xform2d &mat, double width, double height, ClipperLib::Paths &clip_path, PolygonSink &sink) {
    /* For our output to look correct, we have to paint the image's bounding box in black as background for our halftone
     * blobs. We cannot simply draw a rect here, though. Instead we have to first intersect the bounding box with the
     * clip path we get from the caller.
     *
     * First, setup the bounding box rectangle in our local px coordinate space. */
    ClipperLib::Path rect_path;
    for (auto &elem : vector<pair<double, double>> {{0, 0}, {width, 0}, {width, height}, {0, height}}) {
        d2p xf(mat.doc2phys(d2p{elem.first, elem.second}));
        rect_path.push_back({
                (ClipperLib::cInt)round(xf[0] * clipper_scale),
                (ClipperLib::cInt)round(xf[1] * clipper_scale)
        });
    }

    /* Intersect the bounding box with the caller's clip path */
    ClipperLib::Clipper c;
    c.AddPath(rect_path, ClipperLib::ptSubject, /* closed */ true);
    if (!clip_path.empty()) {
        c.AddPaths(clip_path, ClipperLib::ptClip, /* closed */ true);
    }

    ClipperLib::Paths rect_out;
    c.StrictlySimple(true);
    c.Execute(ClipperLib::ctIntersection, rect_out, ClipperLib::pftNonZero, ClipperLib::pftNonZero);

    /* draw into gerber. */
    for (const auto &poly : rect_out) {
        vector<array<double, 2>> out;
        for (const auto &p : poly)
            out.push_back(std::array<double, 2>{
                    ((double)p.X) / clipper_scale, ((double)p.Y) / clipper_scale
                    });
        sink << GRB_POL_CLEAR << out;
    }
}



/* Render image into gerber file.
 *
 * This function renders an image into a number of vector primitives emulating the images grayscale brightness by
 * differently sized vector shaped giving an effect similar to halftone printing used in newspapers.
 *
 * On a high level, this function does this in four steps:
 * 1. It preprocesses the source image at the pixel level. This involves several tasks:
 *    1.1. It converts the image to grayscale.
 *    1.2. It scales the image up or down to match the given minimum feature size.
 *    1.3. It applies a blur depending on the given minimum feature size to prevent aliasing artifacts.
 * 2. It randomly spread points across the image using poisson disc sampling. This yields points that have a fairly even
 *    average distance to each other across the image, and that have a guaranteed minimum distance that depends on
 *    minimum feature size.
 * 3. It calculates a voronoi map based on this set of points and it calculats the polygon shape of each cell of the
 *    voronoi map.
 * 4. It scales each of these voronoi cell polygons to match the input images brightness at the spot covered by this
 *    cell.
 */
void gerbolyze::VoronoiVectorizer::vectorize_image(xform2d &mat, const pugi::xml_node &node, ClipperLib::Paths &clip_path, PolygonSink &sink, double min_feature_size_px) {
    double x, y, width, height;
    parse_img_meta(node, x, y, width, height);
    cv::Mat img = read_img_opencv(node);
    if (img.empty())
        return;

    /* Set up target transform using SVG transform and x/y attributes */
    xform2d local_xf(mat);
    local_xf.translate(x, y);
    local_xf.transform(xform2d(node.attribute("transform").value()));

    double orig_rows = img.rows;
    double orig_cols = img.cols;
    double scale_x = (double)width / orig_cols;
    double scale_y = (double)height / orig_rows;
    double off_x = 0;
    double off_y = 0;
    handle_aspect_ratio(node.attribute("preserveAspectRatio").value(),
            scale_x, scale_y, off_x, off_y, orig_cols, orig_rows);

    /* Adjust minimum feature size given in mm and translate into px document units in our local coordinate system. */
    min_feature_size_px = local_xf.doc2phys_dist(min_feature_size_px);

    draw_bg_rect(local_xf, width, height, clip_path, sink);

    /* Set up a poisson-disc sampled point "grid" covering the image. Calculate poisson disc parameters from given
     * minimum feature size. */
    double grayscale_overhead = 0.8; /* fraction of distance between two adjacent cell centers that is reserved for
                                        grayscale interpolation. Larger values -> better grayscale resolution,
                                        larger cells. */
    double center_distance = min_feature_size_px * 2.0 * (1.0 / (1.0-grayscale_overhead));
    vector<d2p> *grid_centers = get_sampler(m_grid_type)(scale_x * orig_cols, scale_y*orig_rows, center_distance);
    //vector<d2p> *grid_centers = sample_poisson_disc(width, height, min_feature_size_px * 2.0 * 2.0);
    //vector<d2p> *grid_centers = sample_hexgrid(width, height, center_distance);
    //vector<d2p> *grid_centers = sample_squaregrid(width, height, center_distance);

    /* Target factor between given min_feature_size and intermediate image pixels,
     * i.e. <scale_featuresize_factor> px ^= min_feature_size */
    double scale_featuresize_factor = 3.0;
    /* TODO: support for preserveAspectRatio attribute */
    double px_w = width / min_feature_size_px * scale_featuresize_factor;
    double px_h = height / min_feature_size_px * scale_featuresize_factor;

    /* Scale intermediate image (step 1.2) to have <scale_featuresize_factor> pixels per min_feature_size. */ 
    cv::Mat scaled(cv::Size{(int)round(px_w), (int)round(px_h)}, img.type());
    cv::resize(img, scaled, scaled.size(), 0, 0);
    cerr << "scaled " << img.cols << ", " << img.rows << " -> " << scaled.cols << ", " << scaled.rows << endl;
    img.release();

    /* Blur image with a kernel larger than our minimum feature size to avoid aliasing. */
    cv::Mat blurred(scaled.size(), scaled.type());
    int blur_size = (int)ceil(fmax(scaled.cols / width, scaled.rows / height) * center_distance);
    if (blur_size%2 == 0)
        blur_size += 1;
    cerr << "blur size " << blur_size << endl;
    cv::GaussianBlur(scaled, blurred, {blur_size, blur_size}, 0, 0);
    scaled.release();
    
    /* Calculate voronoi diagram for the grid generated above. */
    jcv_diagram diagram;
    memset(&diagram, 0, sizeof(jcv_diagram));
    cerr << "adjusted scale " << scale_x << " " << scale_y << endl;
    cerr << "voronoi clip rect " << (scale_x * orig_cols) << " " << (scale_y * orig_rows) << endl;
    jcv_rect rect {{0.0, 0.0}, {scale_x * orig_cols, scale_y * orig_rows}};
    jcv_point *pts = reinterpret_cast<jcv_point *>(grid_centers->data()); /* hackety hack */
    jcv_diagram_generate(grid_centers->size(), pts, &rect, 0, &diagram);
    /* Relax points, i.e. wiggle them around a little bit to equalize differences between cell sizes a little bit. */
    if (m_relax)
        voronoi_relax_points(&diagram, pts);
    memset(&diagram, 0, sizeof(jcv_diagram));
    jcv_diagram_generate(grid_centers->size(), pts, &rect, 0, &diagram);
    
    /* For each voronoi cell calculated above, find the brightness of the blurred image pixel below its center. We do
     * not have to average over the entire cell's area here: The blur is doing a good approximation of that while being
     * simpler and faster.
     *
     * We do this step before generating the cell poygons below because we have to look up a cell's neighbor's fill
     * factor during gap filling for minimum feature size preservation. */
    vector<double> fill_factors(diagram.numsites); /* Factor to be multiplied with site polygon radius to yield target
                                                      fill level */
    const jcv_site* sites = jcv_diagram_get_sites(&diagram);
    int j = 0;
    for (int i=0; i<diagram.numsites; i++) {
        const jcv_point center = sites[i].p;

        double pxd = (double)blurred.at<unsigned char>(
                (int)round(center.y / (scale_y * orig_rows / blurred.rows)),
                (int)round(center.x / (scale_x * orig_cols / blurred.cols))) / 255.0; 
        /* FIXME: This is a workaround for a memory corruption bug that happens with the square-grid setting. When using
         * square-grid on a fairly small test image, sometimes sites[i].index will be out of bounds here.
         */
        if (sites[i].index < (int)fill_factors.size())
            fill_factors[sites[i].index] = sqrt(pxd);
    }

    /* Minimum gap between adjacent scaled site polygons. */
    double min_gap_px = min_feature_size_px;
    vector<double> adjusted_fill_factors;
    adjusted_fill_factors.reserve(32); /* Vector to hold adjusted fill factors for each edge for gap filling */
    /* now iterate over all voronoi cells again to generate each cell's scaled polygon halftone blob. */
    for (int i=0; i<diagram.numsites; i++) {
        const jcv_point center = sites[i].p;
        double fill_factor_ours = fill_factors[sites[i].index];
        
        /* Do not render halftone blobs that are too small */
        if (fill_factor_ours * 0.5 * center_distance < min_gap_px)
            continue;

        /* Iterate over this cell's edges. For each edge, check the gap that would result between this cell's halftone
         * blob and the neighboring cell's halftone blob based on their fill factors. If the gap is too small, either
         * widen it by adjusting both fill factors down a bit (for this edge only!), or eliminate it by setting both
         * fill factors to 1.0 (again, for this edge only!). */
        adjusted_fill_factors.clear();
        const jcv_graphedge* e = sites[i].edges;
        while (e) {
            /* half distance between both neighbors of this edge, i.e. sites[i] and its neighbor. */
            /* Note that in a voronoi tesselation, this edge is always halfway between. */
            double adjusted_fill_factor = fill_factor_ours;

            if (e->neighbor != nullptr) { /* nullptr -> edge is on the voronoi map's border */
                double rad = sqrt(pow(center.x - e->neighbor->p.x, 2) + pow(center.y - e->neighbor->p.y, 2)) / 2.0;
                double fill_factor_theirs = fill_factors[e->neighbor->index];
                double gap_px = (1.0 - fill_factor_ours) * rad + (1.0 - fill_factor_theirs) * rad;

                if (gap_px > min_gap_px) {
                    /* all good. gap is wider than minimum. */
                } else if (gap_px > 0.5 * min_gap_px) {
                    /* gap is narrower than minimum, but more than half of minimum width. */
                    /* force gap open, distribute adjustment evenly on left/right */
                    double fill_factor_adjustment = (min_gap_px - gap_px) / 2.0 / rad;
                    adjusted_fill_factor -= fill_factor_adjustment;
                } else {
                    /* gap is less than half of minimum width. Force gap closed. */
                    adjusted_fill_factor = 1.0;
                }
            }
            adjusted_fill_factors.push_back(adjusted_fill_factor);
            e = e->next;
        }

        /* Now, generate the actual halftone blob polygon */
        ClipperLib::Path cell_path;
        double last_fill_factor = adjusted_fill_factors.back();
        e = sites[i].edges;
        j = 0;
        while (e) {
            double fill_factor = adjusted_fill_factors[j];
            if (last_fill_factor != fill_factor) {
                /* Fill factor was adjusted since last edge, so generate one extra point so we have a nice radial
                 * "step". */
                d2p p = local_xf.doc2phys(d2p{
                    off_x + center.x + (e->pos[0].x - center.x) * fill_factor,
                    off_y + center.y + (e->pos[0].y - center.y) * fill_factor
                });
                cell_path.push_back({
                        (ClipperLib::cInt)round(p[0] * clipper_scale),
                        (ClipperLib::cInt)round(p[1] * clipper_scale)
                });
            }

            /* Emit endpoint of current edge */
            d2p p = local_xf.doc2phys(d2p{
                off_x + center.x + (e->pos[1].x - center.x) * fill_factor,
                off_y + center.y + (e->pos[1].y - center.y) * fill_factor
            });
            cell_path.push_back({
                    (ClipperLib::cInt)round(p[0] * clipper_scale),
                    (ClipperLib::cInt)round(p[1] * clipper_scale)
            });

            j += 1;
            last_fill_factor = fill_factor;
            e = e->next;
        }

        /* Now, clip the halftone blob generated above against the given clip path. We do this individually for each
         * blob since this way is *much* faster than throwing a million blobs at once at poor clipper. */
        ClipperLib::Paths polys;
        ClipperLib::Clipper c;
        c.AddPath(cell_path, ClipperLib::ptSubject, /* closed */ true);
        if (!clip_path.empty()) {
            c.AddPaths(clip_path, ClipperLib::ptClip, /* closed */ true);
        }
        c.StrictlySimple(true);
        c.Execute(ClipperLib::ctIntersection, polys, ClipperLib::pftNonZero, ClipperLib::pftNonZero);

        /* Export halftone blob to gerber. */
        for (const auto &poly : polys) {
            vector<array<double, 2>> out;
            for (const auto &p : poly)
                out.push_back(std::array<double, 2>{
                        ((double)p.X) / clipper_scale, ((double)p.Y) / clipper_scale
                        });
            sink << GRB_POL_DARK << out;
        }
    }

    blurred.release();
    jcv_diagram_free( &diagram );
    delete grid_centers;
}

void gerbolyze::handle_aspect_ratio(string spec, double &scale_x, double &scale_y, double &off_x, double &off_y, double cols, double rows) {

    if (spec.empty()) {
        spec = "xMidYMid meet";
    }

    auto idx = spec.find(" ");
    string par_align = spec;
    string par_meet = "meet";
    if (idx != string::npos) {
        par_align = spec.substr(0, idx);
        par_meet = spec.substr(idx+1);
    }

    if (par_align != "none") {
        double scale = scale_x;
        if (par_meet == "slice") {
            scale = std::max(scale_x, scale_y);
        } else {
            scale = std::min(scale_x, scale_y);
        }

        std::regex reg("x(Min|Mid|Max)Y(Min|Mid|Max)");
        std::smatch match;

        cerr << "data: " <<" "<< scale_x << "/" << scale_y << ": " << scale << endl;
        off_x = (scale_x - scale) * cols;
        off_y = (scale_y - scale) * rows;
        cerr << rows <<","<<cols<<" " << off_x << "," << off_y << endl;
        if (std::regex_match(par_align, match, reg)) {
            assert (match.size() == 3);
            if (match[1].str() == "Min") {
                off_x = 0;
            } else if (match[1].str() == "Mid") {
                off_x *= 0.5;
            }

            if (match[2].str() == "Min") {
                off_y = 0;
            } else if (match[2].str() == "Mid") {
                off_y *= 0.5;
            }

        } else {
            cerr << "Invalid preserveAspectRatio meetOrSlice value \"" << par_align << "\"" << endl;
            off_x *= 0.5;
            off_y *= 0.5;
        }

        scale_x = scale_y = scale;
    }
    cerr << "res: "<< off_x << "," << off_y << endl;
}


void gerbolyze::OpenCVContoursVectorizer::vectorize_image(xform2d &mat, const pugi::xml_node &node, ClipperLib::Paths &clip_path, PolygonSink &sink, double min_feature_size_px) {
    (void) min_feature_size_px; /* unused by this vectorizer */
    double x, y, width, height;
    parse_img_meta(node, x, y, width, height);
    cv::Mat img = read_img_opencv(node);
    if (img.empty())
        return;

    /* Set up target transform using SVG transform and x/y attributes */
    xform2d local_xf(mat);
    local_xf.transform(xform2d(node.attribute("transform").value()));
    local_xf.translate(x, y);

    double scale_x = (double)width / (double)img.cols;
    double scale_y = (double)height / (double)img.rows;
    double off_x = 0;
    double off_y = 0;
    handle_aspect_ratio(node.attribute("preserveAspectRatio").value(),
            scale_x, scale_y, off_x, off_y, img.cols, img.rows);

    draw_bg_rect(local_xf, width, height, clip_path, sink);

    vector<vector<cv::Point>> contours;
    vector<cv::Vec4i> hierarchy;
    cv::findContours(img, contours, hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_TC89_KCOS);

    queue<pair<size_t, bool>> child_stack;
    child_stack.push({ 0, true });

    while (!child_stack.empty()) {
        bool dark = child_stack.front().second;
        for (int i=child_stack.front().first; i>=0; i = hierarchy[i][0]) {
            if (hierarchy[i][2] >= 0) {
                child_stack.push({ hierarchy[i][2], !dark });
            }

            sink << (dark ? GRB_POL_DARK : GRB_POL_CLEAR);

            bool is_clockwise = cv::contourArea(contours[i], true) > 0;
            if (!is_clockwise)
                std::reverse(contours[i].begin(), contours[i].end());

            ClipperLib::Path out;
            for (const auto &p : contours[i]) {
                d2p q = local_xf.doc2phys(d2p{
                    off_x + (double)p.x * scale_x,
                    off_y + (double)p.y * scale_y
                });
                out.push_back({
                        (ClipperLib::cInt)round(q[0] * clipper_scale),
                        (ClipperLib::cInt)round(q[1] * clipper_scale)
                });
            }

            ClipperLib::Clipper c;
            c.AddPath(out, ClipperLib::ptSubject, /* closed */ true);
            if (!clip_path.empty()) {
                c.AddPaths(clip_path, ClipperLib::ptClip, /* closed */ true);
            }
            c.StrictlySimple(true);
            ClipperLib::Paths polys;
            c.Execute(ClipperLib::ctIntersection, polys, ClipperLib::pftNonZero, ClipperLib::pftNonZero);

            /* Draw into gerber. */
            for (const auto &poly : polys) {
                vector<array<double, 2>> out;
                for (const auto &p : poly)
                    out.push_back(std::array<double, 2>{
                            ((double)p.X) / clipper_scale, ((double)p.Y) / clipper_scale
                            });
                sink << out;
            }
        }

        child_stack.pop();
    }
}

gerbolyze::VectorizerSelectorizer::VectorizerSelectorizer(const string default_vectorizer, const string defs)
    : m_default(default_vectorizer) {
    istringstream foo(defs);
    string elem;
    while (std::getline(foo, elem, ',')) {
        size_t pos = elem.find_first_of("=");
        if (pos == string::npos) {
            cerr << "Error parsing vectorizer selection string at element \"" << elem << "\"" << endl;
            continue;
        }

        const string parsed_id = elem.substr(0, pos);
        const string mapping = elem.substr(pos+1);
        m_map[parsed_id] = mapping;
    }

    cerr << "parsed " << m_map.size() << " vectorizers" << endl;
    for (auto &elem : m_map) {
        cerr << "  " << elem.first << " -> " << elem.second << endl;
    }
}

ImageVectorizer *gerbolyze::VectorizerSelectorizer::select(const pugi::xml_node &img) {
    const string id = img.attribute("id").value();
    cerr << "selecting vectorizer for image \"" << id << "\"" << endl;
    if (m_map.count(id) > 0) {
        cerr << "  -> found" << endl;
        return makeVectorizer(m_map[id]);
    }

    cerr << "  -> default" << endl;
    return makeVectorizer(m_default);
}

