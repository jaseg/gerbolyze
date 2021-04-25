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

SimpleGerberOutput::SimpleGerberOutput(ostream &out, bool only_polys, int digits_int, int digits_frac, double scale, d2p offset, bool flip_polarity, bool outline_mode)
    : StreamPolygonSink(out, only_polys),
    m_digits_int(digits_int),
    m_digits_frac(digits_frac),
    m_offset(offset),
    m_scale(scale),
    m_flip_pol(flip_polarity),
    m_outline_mode(outline_mode),
    m_current_aperture(0.0),
    m_aperture_num(10) /* See gerber standard */
{
    assert(1 <= digits_int && digits_int <= 9);
    assert(0 <= digits_frac && digits_frac <= 9);
    m_gerber_scale = round(pow(10, m_digits_frac));
}

void SimpleGerberOutput::header_impl(d2p origin, d2p size) {
    m_offset[0] += origin[0] * m_scale;
    m_offset[1] += origin[1] * m_scale;
    m_width = (size[0] - origin[0]) * m_scale;
    m_height = (size[1] - origin[1]) * m_scale;
    
    if (pow(10, m_digits_int-1) < max(m_width, m_height)) {
        cerr << "Warning: Input has bounding box too large for " << m_digits_int << "." << m_digits_frac << " gerber resolution!" << endl;
    }

    m_out << "%FSLAX" << m_digits_int << m_digits_frac << "Y" << m_digits_int << m_digits_frac << "*%" << endl;
    m_out << "%MOMM*%" << endl;
    m_out << "%LPD*%" << endl;
    m_out << "G01*" << endl;
    m_out << "%ADD10C,0.050000*%" << endl;
    m_out << "D10*" << endl;
}

SimpleGerberOutput& SimpleGerberOutput::operator<<(const ApertureToken &ap) {
    if (ap.m_size == m_current_aperture) {
        return *this;
    }
    m_current_aperture = ap.m_size;
    m_aperture_num += 1;

    double size = (ap.m_size > 0.0) ? ap.m_size : 0.05;
    m_out << "%ADD" << m_aperture_num << "C," << size << "*%" << endl;
    m_out << "D" << m_aperture_num << "*" << endl;

    return *this;
}

SimpleGerberOutput& SimpleGerberOutput::operator<<(GerberPolarityToken pol) {
    assert(pol == GRB_POL_DARK || pol == GRB_POL_CLEAR);

    if (m_outline_mode) {
        assert(pol == GRB_POL_DARK);
    }

    if ((pol == GRB_POL_DARK) != m_flip_pol) {
        m_out << "%LPD*%" << endl;
    } else {
        m_out << "%LPC*%" << endl;
    }

    return *this;
}
SimpleGerberOutput& SimpleGerberOutput::operator<<(const Polygon &poly) {
    if (poly.size() < 3 && !m_outline_mode) {
        cerr << "Warning: " << poly.size() << "-element polygon passed to SimpleGerberOutput" << endl;
        return *this;
    }

    /* NOTE: Clipper and gerber both have different fixed-point scales. We get points in double mm. */
    double x = round((poly[0][0] * m_scale + m_offset[0]) * m_gerber_scale);
    double y = round((m_height - poly[0][1] * m_scale + m_offset[1]) * m_gerber_scale);
    if (!m_outline_mode) {
        m_out << "G36*" << endl;
    }

    m_out << "X" << setw(m_digits_int + m_digits_frac) << setfill('0') << std::internal /* isn't C++ a marvel of engineering? */ << (long long int)x
          << "Y" << setw(m_digits_int + m_digits_frac) << setfill('0') << std::internal << (long long int)y
          << "D02*" << endl;
    m_out << "G01*" << endl;

    for (size_t i=1; i<poly.size(); i++) {
        double x = round((poly[i][0] * m_scale + m_offset[0]) * m_gerber_scale);
        double y = round((m_height - poly[i][1] * m_scale + m_offset[1]) * m_gerber_scale);
        m_out << "X" << setw(m_digits_int + m_digits_frac) << setfill('0') << std::internal << (long long int)x
              << "Y" << setw(m_digits_int + m_digits_frac) << setfill('0') << std::internal << (long long int)y
              << "D01*" << endl;
    }

    if (!m_outline_mode) {
        m_out << "G37*" << endl;
    }

    return *this;
}

void SimpleGerberOutput::footer_impl() {
    m_out << "M02*" << endl;
}

