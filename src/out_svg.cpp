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
#include <svg_import_defs.h>

using namespace gerbolyze;
using namespace std;

SimpleSVGOutput::SimpleSVGOutput(ostream &out, bool only_polys, int digits_frac, string dark_color, string clear_color)
    : StreamPolygonSink(out, only_polys),
    m_digits_frac(digits_frac),
    m_dark_color(dark_color),
    m_clear_color(clear_color),
    m_current_color(dark_color)
{
}

void SimpleSVGOutput::header_impl(d2p origin, d2p size) {
    m_offset[0] = origin[0];
    m_offset[1] = origin[1];
    m_out << "<svg width=\"" << size[0] << "mm\" height=\"" << size[1] << "mm\" viewBox=\"0 0 "
        << size[0] << " " << size[1] << "\" xmlns=\"http://www.w3.org/2000/svg\">" << endl;
}

SimpleSVGOutput &SimpleSVGOutput::operator<<(GerberPolarityToken pol) {
    if (pol == GRB_POL_DARK) {
        m_current_color = m_dark_color;
    } else if (pol == GRB_POL_CLEAR) {
        m_current_color = m_clear_color;
    } else {
        assert(false);
    }

    return *this;
}

SimpleSVGOutput &SimpleSVGOutput::operator<<(const Polygon &poly) {
    if (poly.size() < 3) {
        cerr << "Warning: " << poly.size() << "-element polygon passed to SimpleGerberOutput" << endl;
        return *this;
    }

    m_out << "<path fill=\"" << m_current_color << "\" d=\"";
    m_out << "M " << setprecision(m_digits_frac) << (poly[0][0] + m_offset[0])
          << " " << setprecision(m_digits_frac) << (poly[0][1] + m_offset[1]);
    for (size_t i=1; i<poly.size(); i++) {
        m_out << " L " << setprecision(m_digits_frac) << (poly[i][0] + m_offset[0])
              << " " << setprecision(m_digits_frac) << (poly[i][1] + m_offset[1]);
    }
    m_out << " Z";
    m_out << "\"/>" << endl;

    return *this;
}

void SimpleSVGOutput::footer_impl() {
    m_out << "</svg>" << endl;
}

