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

#ifndef SVG_COLOR_H
#define SVG_COLOR_H

#include <pugixml.hpp>

namespace svg_plugin {

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

} /* namespace svg_plugin */

#endif /* SVG_COLOR_H */
