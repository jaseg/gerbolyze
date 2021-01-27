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

#include <cairo.h>
#include <pugixml.hpp>
#include <clipper.hpp>
#include <gerbolyze.hpp>
#include "vec_grid.h"

namespace gerbolyze {

    class VoronoiVectorizer : public ImageVectorizer {
    public:
        VoronoiVectorizer(grid_type grid, bool relax=true) : m_relax(relax), m_grid_type(grid) {}

        void vectorize_image(cairo_t *cr, const pugi::xml_node &node, ClipperLib::Paths &clip_path, cairo_matrix_t &viewport_matrix, PolygonSink &sink, double min_feature_size_px);
    private:
        double m_relax;
        grid_type m_grid_type;
    };
}

