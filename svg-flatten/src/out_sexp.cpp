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
#include <ctime>

using namespace gerbolyze;
using namespace std;


KicadSexpOutput::KicadSexpOutput(ostream &out, string mod_name, string layer, bool only_polys, string ref_text, string val_text, d2p ref_pos, d2p val_pos)
    : StreamPolygonSink(out, only_polys),
    m_mod_name(mod_name),
    m_layer(layer == "auto" ? "unknown" : layer),
    m_auto_layer(layer == "auto"),
    m_val_text(val_text),
    m_ref_pos(ref_pos),
    m_val_pos(val_pos)
{
    if (ref_text.empty()) {
        m_ref_text = mod_name;
    } else {
        m_ref_text = ref_text;
    }
}

void KicadSexpOutput::header_impl(d2p, d2p) {
    auto tedit = std::time(0);
    m_out << "(module " << m_mod_name << " (layer F.Cu) (tedit " << std::hex << std::setfill('0') << std::setw(8) << tedit << ")" << endl;
    m_out << "  (fp_text reference " << m_ref_text << " (at " << m_ref_pos[0] << " " << m_ref_pos[1] << ") (layer F.SilkS) hide" << endl;
    m_out << "    (effects (font (size 1 1) (thickness 0.15)))" << endl;
    m_out << "  )" << endl;
    m_out << "  (fp_text value " << m_val_text << " (at " << m_val_pos[0] << " " << m_val_pos[1] << ") (layer F.SilkS) hide" << endl;
    m_out << "    (effects (font (size 1 1) (thickness 0.15)))" << endl;
    m_out << "  )" << endl;
}

KicadSexpOutput &KicadSexpOutput::operator<<(GerberPolarityToken pol) {
    if (pol == GRB_POL_CLEAR) {
        cerr << "Warning: clear polarity not supported since KiCAD manages to have an even worse graphics model than gerber, except it can't excuse itself by its age..... -.-" << endl;
    }

    return *this;
}

KicadSexpOutput &KicadSexpOutput::operator<<(const LayerNameToken &layer_name) {
    if (!m_auto_layer)
        return *this;

    cerr << "Setting S-Exp export layer to \"" << layer_name.m_name << "\"" << endl;
    if (!layer_name.m_name.empty()) {
        m_layer = layer_name.m_name;
    } else {
        m_layer = "unknown";
    }

    return *this;
}

KicadSexpOutput &KicadSexpOutput::operator<<(const Polygon &poly) {
    if (m_auto_layer) {
        if (std::find(m_export_layers->begin(), m_export_layers->end(), m_layer) == m_export_layers->end()) {
            cerr << "Rejecting S-Exp export layer \"" << m_layer << "\"" << endl;
            return *this;
        }
    }

    if (poly.size() < 3) {
        cerr << "Warning: " << poly.size() << "-element polygon passed to KicadSexpOutput" << endl;
        return *this;
    }

    m_out << "  (fp_poly (pts";
    for (auto &p : poly) {
        m_out << " (xy " << p[0] << " " << p[1] << ")";
    }
    m_out << ")";
    m_out << " (layer " << m_layer << ") (width 0))" << endl;

    return *this;
}

void KicadSexpOutput::footer_impl() {
    m_out << ")" << endl;
}


