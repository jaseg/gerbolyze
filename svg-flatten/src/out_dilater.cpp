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

#include <cmath>
#include <algorithm>
#include <string>
#include <iostream>
#include <iomanip>
#include <gerbolyze.hpp>
#include <clipper.hpp>
#include <svg_import_defs.h>
#include <svg_geom.h>
#include "polylinecombine.hpp"

using namespace gerbolyze;
using namespace std;

void Dilater::header(d2p origin, d2p size) {
    m_sink.header(origin, size);
}

void Dilater::footer() {
    m_sink.footer();
}

Dilater &Dilater::operator<<(const LayerNameToken &layer_name) {
    m_sink << layer_name;

    return *this;
}

Dilater &Dilater::operator<<(GerberPolarityToken pol) {
    m_current_polarity = pol;
    m_sink << pol;

    return *this;
}

Dilater &Dilater::operator<<(const Polygon &poly) {
    ClipperLib::Path poly_c;
    for (auto &p : poly) {
        poly_c.push_back({(ClipperLib::cInt)round(p[0] * clipper_scale), (ClipperLib::cInt)round(p[1] * clipper_scale)});
    }

    ClipperLib::ClipperOffset offx;
    offx.ArcTolerance = 0.05 * clipper_scale; /* 10µm; TODO: Make this configurable */
    offx.AddPath(poly_c, ClipperLib::jtRound, ClipperLib::etClosedPolygon);
    double dilation = m_dilation;
    if (m_current_polarity == GRB_POL_CLEAR) {
        dilation = -dilation;
    }

    ClipperLib::PolyTree solution; 
    offx.Execute(solution, dilation * clipper_scale);

    ClipperLib::Paths c_nice_polys;
    dehole_polytree(solution, c_nice_polys);

    for (auto &nice_poly : c_nice_polys) {
        Polygon new_poly;
        for (auto &p : nice_poly) {
            new_poly.push_back({
                    (double)p.X / clipper_scale,
                    (double)p.Y / clipper_scale });
        }
        m_sink << new_poly;
    }

    return *this;
}

