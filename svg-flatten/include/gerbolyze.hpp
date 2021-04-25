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

#include <map>
#include <iostream>
#include <string>
#include <array>

#include <pugixml.hpp>

#include "svg_pattern.h"
#include "geom2d.hpp"

namespace gerbolyze {

    constexpr char lib_version[] = "2.0";

    typedef std::function<std::vector<d2p> *(double, double, double)> sampling_fun;

    enum GerberPolarityToken {
        GRB_POL_CLEAR,
        GRB_POL_DARK
    };

    class LayerNameToken {
    public:
        std::string m_name;
    };

    class ApertureToken {
    public:
        ApertureToken(double size=0.0) : m_size(size) {}
        double m_size = 0.0;
    };

    class PolygonSink {
        public:
            virtual ~PolygonSink() {}
            virtual void header(d2p origin, d2p size) {(void) origin; (void) size;}
            virtual PolygonSink &operator<<(const Polygon &poly) = 0;
            virtual PolygonSink &operator<<(const ClipperLib::Paths paths) {
                for (const auto &poly : paths) {
                    *this << poly;
                }
                return *this;
            };
            virtual PolygonSink &operator<<(const ClipperLib::Path poly) {
                vector<array<double, 2>> out;
                for (const auto &p : poly) {
                    out.push_back(std::array<double, 2>{
                            ((double)p.X) / clipper_scale, ((double)p.Y) / clipper_scale
                    });
                }
                return *this << out;
            };
            virtual PolygonSink &operator<<(const LayerNameToken &) { return *this; };
            virtual PolygonSink &operator<<(GerberPolarityToken pol) = 0;
            virtual PolygonSink &operator<<(const ApertureToken &) { return *this; };
            virtual void footer() {}
    };

    class Flattener_D;
    class Flattener : public PolygonSink {
        public:
            Flattener(PolygonSink &sink);
            virtual ~Flattener();
            virtual void header(d2p origin, d2p size);
            virtual Flattener &operator<<(const Polygon &poly);
            virtual Flattener &operator<<(const LayerNameToken &layer_name);
            virtual Flattener &operator<<(GerberPolarityToken pol);
            virtual void footer();

        private:
            void render_out_clear_polys();
            void flush_polys_to_sink();
            PolygonSink &m_sink;
            GerberPolarityToken m_current_polarity = GRB_POL_DARK;
            Flattener_D *d;
    };

    class Dilater : public PolygonSink {
        public:
            Dilater(PolygonSink &sink, double dilation) : m_sink(sink), m_dilation(dilation) {}
            virtual void header(d2p origin, d2p size);
            virtual Dilater &operator<<(const Polygon &poly);
            virtual Dilater &operator<<(const LayerNameToken &layer_name);
            virtual Dilater &operator<<(GerberPolarityToken pol);
            virtual void footer();

        private:
            PolygonSink &m_sink;
            double m_dilation;
            GerberPolarityToken m_current_polarity = GRB_POL_DARK;
    };

    class StreamPolygonSink : public PolygonSink {
    public:
        StreamPolygonSink(std::ostream &out, bool only_polys=false) : m_only_polys(only_polys), m_out(out) {}
        virtual ~StreamPolygonSink() {}
        virtual void header(d2p origin, d2p size) { if (!m_only_polys) header_impl(origin, size); }
        virtual void footer() { if (!m_only_polys) { footer_impl(); } m_out.flush(); }

    protected:
        virtual void header_impl(d2p origin, d2p size) = 0;
        virtual void footer_impl() = 0;

        bool m_only_polys = false;
        std::ostream &m_out;
    };
    
    extern const std::vector<std::string> kicad_default_layers;

    class ElementSelector {
    public:
        virtual bool match(const pugi::xml_node &node, bool included, bool is_root) const = 0;
    };

    class IDElementSelector : public ElementSelector {
    public:
        virtual bool match(const pugi::xml_node &node, bool included, bool is_root) const;

        std::vector<std::string> include;
        std::vector<std::string> exclude;
        const std::vector<std::string> *layers = nullptr;
    };

    class ImageVectorizer {
    public:
        virtual ~ImageVectorizer() {};
        virtual void vectorize_image(xform2d &mat, const pugi::xml_node &node, ClipperLib::Paths &clip_path, PolygonSink &sink, double min_feature_size_px) = 0;
    };
    
    ImageVectorizer *makeVectorizer(const std::string &name);

    class VectorizerSelectorizer {
    public:
        VectorizerSelectorizer(const std::string default_vectorizer="dev-null", const std::string defs="");

        ImageVectorizer *select(const pugi::xml_node &img);

    private:
        std::string m_default;
        std::map<std::string, std::string> m_map;
    };

    class RenderSettings {
    public:
        double m_minimum_feature_size_mm = 0.1;
        double curve_tolerance_mm;
        VectorizerSelectorizer &m_vec_sel;
        bool outline_mode = false;
    };

    class SVGDocument {
        public:
            SVGDocument() : _valid(false) {}

            /* true -> load successful */
            bool load(std::istream &in);
            bool load(std::string filename);
            /* true -> load successful */
            bool valid() const { return _valid; }
            operator bool() const { return valid(); }

            double mm_to_doc_units(double) const;
            double doc_units_to_mm(double) const;

            double width() const { return page_w_mm; }
            double height() const { return page_h_mm; }

            void render(const RenderSettings &rset, PolygonSink &sink, const ElementSelector *sel=nullptr);
            void render_to_list(const RenderSettings &rset, std::vector<std::pair<Polygon, GerberPolarityToken>> &out, const ElementSelector *sel=nullptr);

        private:
            friend class Pattern;

            const ClipperLib::Paths *lookup_clip_path(const pugi::xml_node &node);
            Pattern *lookup_pattern(const std::string id);

            void export_svg_group(xform2d &mat, const RenderSettings &rset, const pugi::xml_node &group, ClipperLib::Paths &parent_clip_path, const ElementSelector *sel=nullptr, bool included=true, bool is_root=false);
            void export_svg_path(xform2d &mat, const RenderSettings &rset, const pugi::xml_node &node, ClipperLib::Paths &clip_path);
            void setup_viewport_clip();
            void load_clips(const RenderSettings &rset);
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
            ClipperLib::Paths vb_paths; /* viewport clip rect */

            PolygonSink *polygon_sink = nullptr;

            static constexpr double dbg_fill_alpha = 0.8;
            static constexpr double dbg_stroke_alpha = 1.0;
            static constexpr double assumed_usvg_dpi = 96.0;
    };

    typedef std::function<void (const Polygon &, GerberPolarityToken)> lambda_sink_fun;
    class LambdaPolygonSink : public PolygonSink {
    public:
        LambdaPolygonSink(lambda_sink_fun lambda) : m_lambda(lambda) {}

        virtual LambdaPolygonSink &operator<<(const Polygon &poly);
        virtual LambdaPolygonSink &operator<<(GerberPolarityToken pol);
    private:
        GerberPolarityToken m_currentPolarity = GRB_POL_DARK;
        lambda_sink_fun m_lambda;
    };

    class SimpleGerberOutput : public StreamPolygonSink {
    public:
        SimpleGerberOutput(std::ostream &out, bool only_polys=false, int digits_int=4, int digits_frac=6, double scale=1.0, d2p offset={0,0}, bool flip_polarity=false, bool outline_mode=false);
        virtual ~SimpleGerberOutput() {}
        virtual SimpleGerberOutput &operator<<(const Polygon &poly);
        virtual SimpleGerberOutput &operator<<(GerberPolarityToken pol);
        virtual SimpleGerberOutput &operator<<(const ApertureToken &ap);
        virtual void header_impl(d2p origin, d2p size);
        virtual void footer_impl();

    private:
        int m_digits_int;
        int m_digits_frac;
        double m_width;
        double m_height;
        long long int m_gerber_scale;
        d2p m_offset;
        double m_scale;
        bool m_flip_pol;
        bool m_outline_mode;
        double m_current_aperture;
        unsigned int m_aperture_num;
    };

    class SimpleSVGOutput : public StreamPolygonSink {
    public:
        SimpleSVGOutput(std::ostream &out, bool only_polys=false, int digits_frac=6, std::string dark_color="#000000", std::string clear_color="#ffffff");
        virtual ~SimpleSVGOutput() {}
        virtual SimpleSVGOutput &operator<<(const Polygon &poly);
        virtual SimpleSVGOutput &operator<<(GerberPolarityToken pol);
        virtual void header_impl(d2p origin, d2p size);
        virtual void footer_impl();

    private:
        int m_digits_frac;
        std::string m_dark_color;
        std::string m_clear_color;
        std::string m_current_color;
        d2p m_offset;
    };

    class KicadSexpOutput : public StreamPolygonSink {
    public:
        KicadSexpOutput(std::ostream &out, std::string mod_name, std::string layer, bool only_polys=false, std::string m_ref_text="", std::string m_val_text="G*****", d2p ref_pos={0,10}, d2p val_pos={0,-10});
        virtual ~KicadSexpOutput() {}
        virtual KicadSexpOutput &operator<<(const Polygon &poly);
        virtual KicadSexpOutput &operator<<(const LayerNameToken &layer_name);
        virtual KicadSexpOutput &operator<<(GerberPolarityToken pol);
        virtual void header_impl(d2p origin, d2p size);
        virtual void footer_impl();

        void set_export_layers(const std::vector<std::string> &layers) { m_export_layers = &layers; }

    private:
        const std::vector<std::string> *m_export_layers = &kicad_default_layers;
        std::string m_mod_name;
        std::string m_layer;
        bool m_auto_layer;
        std::string m_ref_text;
        std::string m_val_text;
        d2p m_ref_pos;
        d2p m_val_pos;
    };
}
