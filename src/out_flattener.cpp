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
#include <clipper.hpp>
#include <gerbolyze.hpp>
#include <svg_import_defs.h>
#include <svg_geom.h>

using namespace gerbolyze;
using namespace std;

namespace gerbolyze {
    class Flattener_D {
    public:
        ClipperLib::Clipper c;
    };
}

Flattener::Flattener(PolygonSink &sink) : m_sink(sink) {
    d = new Flattener_D();
    d->c.StrictlySimple(true);
}

Flattener::~Flattener() {
    delete d;
}

void Flattener::header(d2p origin, d2p size) {
    m_sink.header(origin, size);
}

Flattener &Flattener::operator<<(GerberPolarityToken pol) {
    if (m_current_polarity != pol) {
        m_current_polarity = pol;
    }

    return *this;
}

Flattener &Flattener::operator<<(const Polygon &poly) {
    ClipperLib::Path le_path;
    for (auto &p : poly) {
        le_path.push_back({(ClipperLib::cInt)round(p[0] * clipper_scale), (ClipperLib::cInt)round(p[1] * clipper_scale)});
    }

    ClipperLib::Paths out;

    if (m_current_polarity == GRB_POL_DARK) {
        d->c.AddPath(le_path, ClipperLib::ptSubject, true);
        d->c.Execute(ClipperLib::ctUnion, out, ClipperLib::pftNonZero);

    } else { /* clear */
        d->c.AddPath(le_path, ClipperLib::ptClip, true);
        d->c.Execute(ClipperLib::ctDifference, out, ClipperLib::pftNonZero);
    }

    d->c.Clear();
    d->c.AddPaths(out, ClipperLib::ptSubject, true);

    return *this;
}

void Flattener::footer() {
    ClipperLib::PolyTree t_out;
    d->c.Execute(ClipperLib::ctDifference, t_out, ClipperLib::pftNonZero);
    d->c.Clear();

    m_sink << GRB_POL_DARK;

    ClipperLib::Paths out;
    cerr << "deholing" << endl;
    dehole_polytree(t_out, out);
    for (auto &poly : out) {
        Polygon poly_out;
        for (auto &p : poly) {
            poly_out.push_back({p.X / clipper_scale, p.Y / clipper_scale});
        }
        m_sink << poly_out;
    }

    m_sink.footer();
}

