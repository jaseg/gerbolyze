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

#pragma once

#include <string>

#include <pugixml.hpp>
#include <clipper.hpp>

#include "svg_import_util.h"
#include "geom2d.hpp"

namespace gerbolyze {

class SVGDocument;
class RenderSettings;

class Pattern {
public:
    Pattern() {}
    Pattern(const pugi::xml_node &node, SVGDocument &doc);

    void tile (xform2d &mat, const gerbolyze::RenderSettings &rset, ClipperLib::Paths &clip);

private:
    double x, y, w, h;
    double vb_x, vb_y, vb_w, vb_h;
    bool has_vb;
    xform2d patternTransform;
    xform2d patternTransform_inv;
    enum RelativeUnits patternUnits;
    enum RelativeUnits patternContentUnits;
    const pugi::xml_node _node;
    SVGDocument *doc = nullptr;
};

} /* namespace gerbolyze */

