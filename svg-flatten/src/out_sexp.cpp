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


/* Note: These values come from KiCAD's common/lset.cpp. KiCAD uses *multiple different names* for the same layer in
 * different places, and not all of them are stable. Sometimes, these names change without notice. If this list isn't
 * up-to-date, it's not my fault. Still, please file an issue. */
const std::vector<std::string> gerbolyze::kicad_default_layers ({
        /* Copper */
        "F.Cu",
        "In1.Cu", "In2.Cu", "In3.Cu", "In4.Cu", "In5.Cu", "In6.Cu", "In7.Cu", "In8.Cu",
        "In9.Cu", "In10.Cu", "In11.Cu", "In12.Cu", "In13.Cu", "In14.Cu", "In15.Cu", "In16.Cu",
        "In17.Cu", "In18.Cu", "In19.Cu", "In20.Cu", "In21.Cu", "In22.Cu", "In23.Cu",
        "In24.Cu", "In25.Cu", "In26.Cu", "In27.Cu", "In28.Cu", "In29.Cu", "In30.Cu",
        "B.Cu",

        /* Technical layers */
        "B.Adhes", "F.Adhes",
        "B.Paste", "F.Paste",
        "B.SilkS", "F.SilkS",
        "B.Mask", "F.Mask",

        /* User layers */
        "Dwgs.User",
        "Cmts.User",
        "Eco1.User", "Eco2.User",
        "Edge.Cuts",
        "Margin",

        /* Footprint layers */
        "F.CrtYd", "B.CrtYd",
        "F.Fab", "B.Fab",

        /* Layers for user scripting etc. */
        "User.1", "User.2", "User.3", "User.4", "User.5", "User.6", "User.7", "User.8", "User.9",
    });


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

KicadSexpOutput &KicadSexpOutput::operator<<(const DrillToken &tok) {
    return *this;
}

void KicadSexpOutput::footer_impl() {
    m_out << ")" << endl;
}

