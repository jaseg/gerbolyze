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

#include <pugixml.hpp>

namespace gerbolyze {

/* Enum that describes the color with which an SVG primite should be exported */
enum gerber_color { 
    GRB_NONE = 0,
    GRB_CLEAR,
    GRB_DARK,
    GRB_PATTERN_FILL,
};

class RGBColor {
public:
    float r, g, b;
    RGBColor(std::string hex);
};

class HSVColor {
public:
    float h, s, v;
    HSVColor(const RGBColor &color);
};

enum gerber_color svg_color_to_gerber(std::string color, std::string opacity, enum gerber_color default_val);
enum gerber_color gerber_color_invert(enum gerber_color color);
enum gerber_color gerber_fill_color(const pugi::xml_node &node);
enum gerber_color gerber_stroke_color(const pugi::xml_node &node);

} /* namespace gerbolyze */

