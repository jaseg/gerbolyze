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
#include <time.h>
#include <gerbolyze.hpp>
#include <svg_import_defs.h>

using namespace gerbolyze;
using namespace std;

SimpleGDSIIOutput::SimpleGDSIIOutput(ostream &out, bool only_polys=false, double scale, d2p offset, bool flip_polarity, std::string libname)
    : StreamPolygonSink(out, only_polys),
    m_offset(offset),
    m_scale(scale),
    m_flip_pol(flip_polarity),
    m_libname(libname)
{
}

void SimpleGDSIIOutput::header_impl(d2p origin, d2p size) {
    m_offset[0] += origin[0] * m_scale;
    m_offset[1] += origin[1] * m_scale;
    m_width = (size[0] - origin[0]) * m_scale;
    m_height = (size[1] - origin[1]) * m_scale;
    
    gds_wr16(GDS_HEADER, {600});

    time_t t = time(NULL);
    struct tm;
    gmtime_r(&t, &tm);
    gds_wr16(GDS_BGNLIB, {tm.tm_year, tm.tm_month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                          tm.tm_year, tm.tm_month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec});

    gds_wr_str(GDS_LIBNAME, m_libname);
}

void gds_wr_d(uint16_t tag, double value) {
    uint64_t d_ul = reinterpret_cast<uint64_t>(value);
    uint64_t sign = !!(casted & (1ULL<<63));
    int exp = (casted >> 52) & 0x7ffULL;
    uint64_t mant = (casted & ((1ULL<<52)-1)) | (1ULL<<52);

    int new_exp = (exp - 1023) / 4 + 64;
    int exp_mod = (exp + 1) % 4;
    uint64_t new_mant = mant * (1<<exp_mod);

    gds_wr16
}

SimpleGDSIIOutput& SimpleGDSIIOutput::operator<<(GerberPolarityToken pol) {
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
SimpleGDSIIOutput& SimpleGDSIIOutput::operator<<(const Polygon &poly) {
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

void SimpleGDSIIOutput::footer_impl() {
    
}

