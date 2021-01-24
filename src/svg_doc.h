/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2021 Jan Sebastian Götte <kicad@jaseg.de>
 * Copyright (C) 2021 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef SVG_DOC_H
#define SVG_DOC_H

#include <map>

#include <pugixml.hpp>

#include "svg_pattern.h"

namespace svg_plugin {

    typedef std::function<void (std::vector<std::array<double, 2>>, bool)> polygon_sink_fun;

    class SVGDocument {
    public:
        SVGDocument() : _valid(false) {}
        SVGDocument(polygon_sink_fun sink_fun) : _valid(false), polygon_sink(sink_fun) {}
        ~SVGDocument();

        /* true -> load successful */
        bool load(std::string filename, std::string debug_out_filename="/tmp/kicad_svg_debug.svg");
        /* true -> load successful */
        bool valid() const { return _valid; }
        operator bool() const { return valid(); }

        double mm_to_doc_units(double) const;
        double doc_units_to_mm(double) const;

        double width() const { return page_w_mm; }
        double height() const { return page_h_mm; }

        void do_export(std::string debug_out_filename="");

    private:
        friend class Pattern;

        cairo_t *cairo() { return cr; }
        const ClipperLib::Paths *lookup_clip_path(const pugi::xml_node &node);
        Pattern *lookup_pattern(const std::string id);

        void export_svg_group(const pugi::xml_node &group, ClipperLib::Paths &parent_clip_path);
        void export_svg_path(const pugi::xml_node &node, ClipperLib::Paths &clip_path);
        void setup_debug_output(std::string filename="");
        void setup_viewport_clip();
        void load_clips();
        void load_patterns();

        bool _valid;
        pugi::xml_document svg_doc;
        pugi::xml_node root_elem;
        pugi::xml_node defs_node;
        double vb_x, vb_y, vb_w, vb_h;
        double page_w, page_h;
        double page_w_mm, page_h_mm;
        std::map<std::string, Pattern> pattern_map;
        std::map<std::string, ClipperLib::Paths> clip_path_map;
        cairo_matrix_t viewport_matrix;
        ClipperLib::Paths vb_paths; /* viewport clip rect */

        cairo_t *cr = nullptr;
        cairo_surface_t *surface = nullptr;

        polygon_sink_fun polygon_sink;

        static constexpr double dbg_fill_alpha = 0.8;
        static constexpr double dbg_stroke_alpha = 1.0;
        static constexpr double assumed_usvg_dpi = 96.0;
    };

} /* namespace svg_plugin */

#endif /* SVG_DOC_H */

