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

#include <vector>
#include "svg_geom.h"
#include "geom2d.hpp"

namespace gerbolyze {
void load_svg_path(xform2d &mat, const pugi::xml_node &node, ClipperLib::PolyTree &ptree_stroke, ClipperLib::PolyTree &ptree_fill, double curve_tolerance);
void parse_dasharray(const pugi::xml_node &node, std::vector<double> &out);
void dash_path(const ClipperLib::Path &in, ClipperLib::Paths &out, const std::vector<double> dasharray, double dash_offset=0.0);
}

