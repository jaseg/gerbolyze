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

#ifndef SVG_PATTERN_H
#define SVG_PATTERN_H

#include <string>
#include <cairo.h>

#include <pugixml.hpp>
#include <clipper.hpp>

#include "svg_import_util.h"

namespace svg_plugin {

class SVGDocument;

class Pattern {
public:
    Pattern() {}
    Pattern(const pugi::xml_node &node, SVGDocument &doc);

    void tile (ClipperLib::Paths &clip);

private:
    double x, y, w, h;
    double vb_x, vb_y, vb_w, vb_h;
    bool has_vb;
    std::string patternTransform;
    enum RelativeUnits patternUnits;
    enum RelativeUnits patternContentUnits;
    const pugi::xml_node _node;
    SVGDocument *doc = nullptr;
};

} /* namespace svg_plugin */

#endif /* SVG_PATTERN_H */
