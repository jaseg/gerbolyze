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

#ifndef SVG_PATH_H
#define SVG_PATH_H

#include <vector>
#include <cairo.h>
#include "svg_geom.h"

namespace svg_plugin {
void load_svg_path(cairo_t *cr, const pugi::xml_node &node, ClipperLib::PolyTree &ptree);
void parse_dasharray(const pugi::xml_node &node, std::vector<double> &out);
void dash_path(const ClipperLib::Path &in, ClipperLib::Paths &out, const std::vector<double> dasharray, double dash_offset=0.0);
}

#endif /* SVG_PATH_H */
