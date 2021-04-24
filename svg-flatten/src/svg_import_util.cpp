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
#include "base64.h"
#include "svg_import_util.h"

using namespace std;

/* Read a double value formatted like usvg formats doubles from an SVG attribute */
double gerbolyze::usvg_double_attr(const pugi::xml_node &node, const char *attr, double default_value) {
    const auto *val = node.attribute(attr).value();
    if (*val == '\0')
        return default_value;

    return atof(val);
}

/* Read an url from an usvg attribute */
string gerbolyze::usvg_id_url(string attr) {
    if (attr.rfind("url(#", 0) == string::npos)
        return string();

    attr = attr.substr(strlen("url(#"));
    attr = attr.substr(0, attr.size()-1);
    return attr;
}

gerbolyze::RelativeUnits gerbolyze::map_str_to_units(string str, gerbolyze::RelativeUnits default_val) {
    if (str == "objectBoundingBox")
        return SVG_ObjectBoundingBox;
    else if (str == "userSpaceOnUse")
        return SVG_UserSpaceOnUse;
    return default_val;
}

/* Cf. https://tools.ietf.org/html/rfc2397 */
string gerbolyze::parse_data_iri(const string &data_url) {
    if (data_url.rfind("data:", 0) == string::npos) /* check if url starts with "data:" */
        return string();

    size_t foo = data_url.find("base64,");
    if (foo == string::npos) /* check if this is actually a data URL */
        return string();

    size_t b64_begin = data_url.find_first_not_of(" ", foo + strlen("base64,"));
    assert(b64_begin != string::npos);

    return base64_decode(data_url.substr(b64_begin));
}

