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
#include <clipper.hpp>
#include <gerbolyze.hpp>
#include "vec_grid.h"

namespace gerbolyze {

    class VoronoiVectorizer : public ImageVectorizer {
    public:
        VoronoiVectorizer(grid_type grid, bool relax=true) : m_relax(relax), m_grid_type(grid) {}

        virtual void vectorize_image(xform2d &mat, const pugi::xml_node &node, ClipperLib::Paths &clip_path, PolygonSink &sink, double min_feature_size_px);
    private:
        double m_relax;
        grid_type m_grid_type;
    };

    class OpenCVContoursVectorizer : public ImageVectorizer {
    public:
        OpenCVContoursVectorizer() {}

        virtual void vectorize_image(xform2d &mat, const pugi::xml_node &node, ClipperLib::Paths &clip_path, PolygonSink &sink, double min_feature_size_px);
    };

    class DevNullVectorizer : public ImageVectorizer {
    public:
        DevNullVectorizer() {}

        virtual void vectorize_image(xform2d &, const pugi::xml_node &, ClipperLib::Paths &, PolygonSink &, double) {}
    };

    void parse_img_meta(const pugi::xml_node &node, double &x, double &y, double &width, double &height);
    std::string read_img_data(const pugi::xml_node &node);
    void draw_bg_rect(xform2d &mat, double width, double height, ClipperLib::Paths &clip_path, PolygonSink &sink);
    void handle_aspect_ratio(std::string spec, double &scale_x, double &scale_y, double &off_x, double &off_y, double cols, double rows);
}

