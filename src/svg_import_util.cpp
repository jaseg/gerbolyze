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

#include <cmath>
#include "base64.h"
#include "svg_import_util.h"

using namespace std;

void svg_plugin::print_matrix(cairo_t *cr, bool print_examples) {
    cairo_matrix_t mat;
    cairo_get_matrix(cr, &mat);
    cerr << "    xform matrix = { xx=" << mat.xx << ", yx=" << mat.yx << ", xy=" << mat.xy << ", yy=" << mat.yy << ", x0=" << mat.x0 << ", y0=" << mat.y0 << " }" << endl;
    if (print_examples) {
        double x=0, y=0;
        cairo_user_to_device(cr, &x, &y);
        cerr << "        (0, 0) -> (" << x << ", " << y << ")" << endl;
        x = 1, y = 0;
        cairo_user_to_device(cr, &x, &y);
        cerr << "        (1, 0) -> (" << x << ", " << y << ")" << endl;
        x = 0, y = 1;
        cairo_user_to_device(cr, &x, &y);
        cerr << "        (0, 1) -> (" << x << ", " << y << ")" << endl;
        x = 1, y = 1;
        cairo_user_to_device(cr, &x, &y);
        cerr << "        (1, 1) -> (" << x << ", " << y << ")" << endl;
    }
}

/* Read a double value formatted like usvg formats doubles from an SVG attribute */
double svg_plugin::usvg_double_attr(const pugi::xml_node &node, const char *attr, double default_value) {
    const auto *val = node.attribute(attr).value();
    if (*val == '\0')
        return default_value;

    return atof(val);
}

/* Read an url from an usvg attribute */
string svg_plugin::usvg_id_url(string attr) {
    if (attr.rfind("url(#", 0) == string::npos)
        return string();

    attr = attr.substr(strlen("url(#"));
    attr = attr.substr(0, attr.size()-1);
    return attr;
}

svg_plugin::RelativeUnits svg_plugin::map_str_to_units(string str, svg_plugin::RelativeUnits default_val) {
    if (str == "objectBoundingBox")
        return SVG_ObjectBoundingBox;
    else if (str == "userSpaceOnUse")
        return SVG_UserSpaceOnUse;
    return default_val;
}

void svg_plugin::load_cairo_matrix_from_svg(const string &transform, cairo_matrix_t &mat) {
    if (transform.empty()) {
        cairo_matrix_init_identity(&mat);
        return;
    }

    string start("matrix(");
    assert(transform.substr(0, start.length()) == start);
    assert(transform.back() == ')');
    const string &foo = transform.substr(start.length(), transform.length());
    const string &bar = foo.substr(0, foo.length() - 1);

    istringstream xform(bar);

    double a, c, e,
           b, d, f;
    xform >> a >> b >> c >> d >> e >> f;
    assert(!xform.fail());

    cairo_matrix_init(&mat, a, b, c, d, e, f);
}

void svg_plugin::apply_cairo_transform_from_svg(cairo_t *cr, const string &transform) {
    cairo_matrix_t mat;
    load_cairo_matrix_from_svg(transform, mat);
    cairo_transform(cr, &mat); /* or cairo_transform? */
}

/* Cf. https://tools.ietf.org/html/rfc2397 */
string svg_plugin::parse_data_iri(const string &data_url) {
    if (data_url.rfind("data:", 0) == string::npos) /* check if url starts with "data:" */
        return string();

    size_t foo = data_url.find("base64,");
    if (foo == string::npos) /* check if this is actually a data URL */
        return string();

    size_t b64_begin = data_url.find_first_not_of(" ", foo + strlen("base64,"));
    assert(b64_begin != string::npos);

    return base64_decode(data_url.substr(b64_begin));
}

/* for debug svg output */
void svg_plugin::apply_viewport_matrix(cairo_t *cr, cairo_matrix_t &viewport_matrix) {
    /* Multiply viewport matrix *from the left*, i.e. as if it had been applied *before* the currently set matrix. */
    cairo_matrix_t old_matrix;
    cairo_get_matrix(cr, &old_matrix);
    cairo_set_matrix(cr, &viewport_matrix);
    cairo_transform(cr, &old_matrix);
}

