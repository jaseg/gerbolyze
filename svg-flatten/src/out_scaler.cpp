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

using namespace gerbolyze;
using namespace std;

/* FIXME thoroughly test ApertureToken scale handling */
void PolygonScaler::header(d2p origin, d2p size) {
    m_sink.header({origin[0] * m_scale, origin[1] * m_scale}, {size[0] * m_scale, size[1] * m_scale});
}

void PolygonScaler::footer() {
    m_sink.footer();
}

PolygonScaler &PolygonScaler::operator<<(const LayerNameToken &layer_name) {
    m_sink << layer_name;

    return *this;
}

PolygonScaler &PolygonScaler::operator<<(GerberPolarityToken pol) {
    m_sink << pol;
    return *this;
}

PolygonScaler &PolygonScaler::operator<<(const ApertureToken &tok) {
    m_sink << ApertureToken(tok.m_size * m_scale);
    return *this;
}

PolygonScaler &PolygonScaler::operator<<(const Polygon &poly) {
    Polygon new_poly;
    for (auto &p : poly) {
        new_poly.push_back({ p[0] * m_scale, p[1] * m_scale });
    }
    m_sink << new_poly;

    return *this;
}

PolygonScaler &PolygonScaler::operator<<(const DrillToken &tok) {
    d2p new_center { tok.m_center[0] * m_scale, tok.m_center[1] * m_scale };
    m_sink << DrillToken(new_center);
    return *this;
}
