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

#include <clipper.hpp>
#include <pugixml.hpp>

namespace gerbolyze {

    ClipperLib::IntRect get_paths_bounds(const ClipperLib::Paths &paths);
    enum ClipperLib::PolyFillType clipper_fill_rule(const pugi::xml_node &node);
    enum ClipperLib::EndType clipper_end_type(const pugi::xml_node &node);
    enum ClipperLib::JoinType clipper_join_type(const pugi::xml_node &node);
    void dehole_polytree(ClipperLib::PolyTree &ptree, ClipperLib::Paths &out);
    void combine_clip_paths(ClipperLib::Paths &in_a, ClipperLib::Paths &in_b, ClipperLib::Paths &out);

} /* namespace gerbolyze */

