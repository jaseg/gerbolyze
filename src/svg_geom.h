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

#ifndef SVG_GEOM_H
#define SVG_GEOM_H

#include <cairo.h>
#include <clipper.hpp>
#include <pugixml.hpp>

namespace svg_plugin {

ClipperLib::IntRect get_paths_bounds(const ClipperLib::Paths &paths);
enum ClipperLib::PolyFillType clipper_fill_rule(const pugi::xml_node &node);
enum ClipperLib::EndType clipper_end_type(const pugi::xml_node &node);
enum ClipperLib::JoinType clipper_join_type(const pugi::xml_node &node);
void dehole_polytree(ClipperLib::PolyNode &ptree, ClipperLib::Paths &out);
void combine_clip_paths(ClipperLib::Paths &in_a, ClipperLib::Paths &in_b, ClipperLib::Paths &out);
void transform_paths(cairo_t *cr, ClipperLib::Paths &paths, cairo_matrix_t *mat=nullptr);

} /* namespace svg_plugin */

#endif /* SVG_GEOM_H */
