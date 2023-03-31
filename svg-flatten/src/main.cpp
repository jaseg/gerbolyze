
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <algorithm>
#include <string>
#include <argagg.hpp>
#include <gerbolyze.hpp>
#include "vec_core.h"
#include <base64.h>
#include "util.h"

using argagg::parser_results;
using argagg::parser;
using namespace std;
using namespace gerbolyze;

string temp_file_path(const char *suffix) {
    ifstream rnd;
    rnd.open("/dev/urandom", ios::in | ios::binary);

    char fn_buf[8];
    rnd.read(fn_buf, sizeof(fn_buf));

    if (rnd.rdstate()) {
        cerr << "Error getting random data for temporary file name" << endl;
        abort();
    }

    ostringstream out;
    out << "tmp_";
    for (size_t i=0; i<sizeof(fn_buf); i++) {
        out << setfill('0') << setw(2) << setbase(16) << static_cast<int>(fn_buf[i] & 0xff);
    }
    out << suffix;

    //cerr << "out \"" << out.str() << "\"" << endl;
#ifndef WASI
    filesystem::path base = filesystem::temp_directory_path();
    return (base / out.str()).native();
#else
    return "/tmp/" + out.str();
#endif
}

int main(int argc, char **argv) {
    parser argparser {{
            {"help", {"-h", "--help"},
                "Print help and exit",
                0},
            {"version", {"-v", "--version"},
                "Print version and exit",
                0},
            {"ofmt", {"-o", "--format"},
                "Output format. Supported: gerber, gerber-outline (for board outline layer), svg, s-exp (KiCAD S-Expression)",
                1},
            {"precision", {"-p", "--precision"},
                "Number of decimal places use for exported coordinates (gerber: 1-9, SVG: 0-*)",
                1},
            {"svg_clear_color", {"--clear-color"},
                "SVG color to use for \"clear\" areas (SVG output only; default: white)",
                1},
            {"svg_dark_color", {"--dark-color"},
                "SVG color to use for \"dark\" areas (SVG output only; default: black)",
                1},
            {"flip_gerber_polarity", {"-f", "--flip-gerber-polarity"},
                "Flip polarity of all output gerber primitives for --format gerber.",
                0},
            {"flip_svg_color_interpretation", {"-i", "--svg-white-is-gerber-dark"},
                "Flip polarity of SVG color interpretation. This affects only SVG primitives like paths and NOT embedded bitmaps. With -i: white -> silk there/\"dark\" gerber primitive.",
                0},
            {"pattern_complete_tiles_only", {"--pattern-complete-tiles-only"},
                "Break SVG spec by only rendering complete pattern tiles, i.e. pattern tiles that entirely fit the target area, instead of performing clipping.",
                0},
            {"use_apertures_for_patterns", {"--use-apertures-for-patterns"},
                "Try to use apertures to represent svg patterns where possible.",
                0},
            {"min_feature_size", {"-d", "--trace-space"},
                "Minimum feature size of elements in vectorized graphics (trace/space) in mm. Default: 0.1mm.",
                1},
            {"geometric_tolerance", {"-t", "--tolerance"},
                "Tolerance in mm for geometric approximation such as curve flattening. Default: 0.1mm.",
                1},
            {"stroke_width_cutoff", {"--min-stroke-width"},
                "Don't render strokes thinner than the given width in mm. Default: 0.01mm.",
                1},
            {"no_stroke_interpolation", {"--no-stroke-interpolation"},
                "Always outline SVG strokes as regions instead of rendering them using Geber interpolation commands where possible.",
                0},
            {"drill_test_polsby_popper_tolerance", {"--drill-test-tolerance"},
                "Tolerance for identifying circles as drills in outline mode",
                1},
            {"aperture_circle_test_tolerance", {"--circle-test-tolerance"},
                "Tolerance for identifying circles as apertures in patterns (--use-apertures-for-patterns)",
                1},
            {"aperture_rect_test_tolerance", {"--rect-test-tolerance"},
                "Tolerance for identifying rectangles as apertures in patterns (--use-apertures-for-patterns)",
                1},
            {"no_header", {"--no-header"},
                "Do not export output format header/footer, only export the primitives themselves",
                0},
            {"flatten", {"--flatten"},
                "Flatten output so it only consists of non-overlapping white polygons. This perform composition at the vector level. Potentially slow.",
                0},
            {"no_flatten", {"--no-flatten"},
                "Disable automatic flattening for KiCAD S-Exp export",
                0},
            {"dilate", {"--dilate"},
                "Dilate output gerber primitives by this amount in mm. Used for masking out other layers.",
                1},
            {"only_groups", {"-g", "--only-groups"},
                "Comma-separated list of group IDs to export.",
                1},
            {"vectorizer", {"-b", "--vectorizer"},
                "Vectorizer to use for bitmap images. One of poisson-disc (default), hex-grid, square-grid, binary-contours, dev-null.",
                1},
            {"vectorizer_map", {"--vectorizer-map"},
                "Map from image element id to vectorizer. Overrides --vectorizer. Format: id1=vectorizer,id2=vectorizer,...",
                1},
            {"force_svg", {"--force-svg"},
                "Force SVG input irrespective of file name",
                0},
            {"force_png", {"--force-png"},
                "Force bitmap graphics input irrespective of file name",
                0},
            {"size", {"-s", "--size"},
                "Bitmap mode only: Physical size of output image in mm. Format: 12.34x56.78",
                1},
            {"sexp_mod_name", {"--sexp-mod-name"},
                "Module name for KiCAD S-Exp output",
                1},
            {"sexp_layer", {"--sexp-layer"},
                "Layer for KiCAD S-Exp output. Defaults to auto-detect layers from SVG layer/top-level group names",
                1},
            {"preserve_aspect_ratio", {"-a", "--preserve-aspect-ratio"},
                "Bitmap mode only: Preserve aspect ratio of image. Allowed values are meet, slice. Can also parse full SVG preserveAspectRatio syntax.",
                1},
            {"skip_usvg", {"--no-usvg"},
                "Do not preprocess input using usvg (do not use unless you know *exactly* what you're doing)",
                0},
            {"scale", {"--scale"},
                "Scale input SVG by the given factor.",
                1},
            {"gerber_scale", {"--gerber-scale"},
                "Scale Gerber output coordinates by the given factor.",
                1},
            {"exclude_groups", {"-e", "--exclude-groups"},
                "Comma-separated list of group IDs to exclude from export. Takes precedence over --only-groups.",
                1},
            /* Forwarded USVG options */
            {"usvg-dpi", {"--usvg-dpi"},
                "Passed through to usvg's --dpi, in case the input file has different ideas of DPI than usvg has.",
                1},
            {"usvg-font-family",       {"--usvg-font-family"}, "", 1},
            {"usvg-font-size",         {"--usvg-font-size"}, "", 1},
            {"usvg-serif-family",      {"--usvg-serif-family"}, "", 1},
            {"usvg-sans-serif-family", {"--usvg-sans-serif-family"}, "", 1},
            {"usvg-cursive-family",    {"--usvg-cursive-family"}, "", 1},
            {"usvg-fantasy-family",    {"--usvg-fantasy-family"}, "", 1},
            {"usvg-monospace-family",  {"--usvg-monospace-family"}, "", 1},
            {"usvg-use-font-file",     {"--usvg-use-font-file"}, "", 1},
            {"usvg-use-fonts-dir",     {"--usvg-use-fonts-dir"}, "", 1},
            {"usvg-skip-system-fonts", {"--usvg-skip-system-fonts"}, "", 0},
    }};


    ostringstream usage;
    usage
        << argv[0] << " " << lib_version << endl
        << endl
        << "Usage: " << argv[0] << " [options]... [input_file] [output_file]" << endl
        << endl
        << "Specify \"-\" for stdin/stdout." << endl
        << endl;

    argagg::parser_results args;
    args = argparser.parse(argc, argv);

    if (args["help"]) {
        argagg::fmt_ostream fmt(cerr);
        fmt << usage.str() << argparser;
        return EXIT_SUCCESS;
    }

    if (args["version"]) {
        cerr << lib_version << endl;
        return EXIT_SUCCESS;
    }

    string in_f_name;
    istream *in_f = &cin;
    ifstream in_f_file;
    string out_f_name;
    ostream *out_f = &cout;
    ofstream out_f_file;

    if (args.pos.size() >= 1) {
        in_f_name = args.pos[0];

        if (args.pos.size() >= 2) {
            out_f_name = args.pos[1];
        }
    }

    if (!in_f_name.empty() && in_f_name != "-") {
        in_f_file.open(in_f_name);
        if (!in_f_file) {
            cerr << "Cannot open input file \"" << in_f_name << "\"" << endl;
            return EXIT_FAILURE;
        }
        in_f = &in_f_file;
    }

    if (!out_f_name.empty() && out_f_name != "-") {
        out_f_file.open(out_f_name);
        if (!out_f_file) {
            cerr << "Cannot open output file \"" << out_f_name << "\"" << endl;
            return EXIT_FAILURE;
        }
        out_f = &out_f_file;
    }

    bool only_polys = args["no_header"];

    int precision = 6;
    if (args["precision"]) {
        precision = atoi(args["precision"]);
    }

    string fmt = args["ofmt"] ? args["ofmt"].as<string>() : "gerber";
    transform(fmt.begin(), fmt.end(), fmt.begin(), [](unsigned char c){ return std::tolower(c); }); /* c++ yeah */

    string sexp_layer = args["sexp_layer"] ? args["sexp_layer"].as<string>() : "auto";

    bool force_flatten = false;
    bool is_sexp = false;
    bool outline_mode = false;
    PolygonSink *sink = nullptr;
    PolygonSink *flattener = nullptr;
    PolygonSink *dilater = nullptr;
    //cerr << "Render sink stack:" << endl;
    if (fmt == "svg") {
        string dark_color = args["svg_dark_color"] ? args["svg_dark_color"].as<string>() : "#000000";
        string clear_color = args["svg_clear_color"] ? args["svg_clear_color"].as<string>() : "#ffffff";
        sink = new SimpleSVGOutput(*out_f, only_polys, precision, dark_color, clear_color);
        //cerr << "  * SVG sink " << endl;

    } else if (fmt == "gbr" || fmt == "grb" || fmt == "gerber" || fmt == "gerber-outline") {
        outline_mode = fmt == "gerber-outline";

        double gerber_scale = args["scale"].as<double>(1.0);
        if (gerber_scale != 1.0) {
            cerr << "Info: Scaling gerber output @gerber_scale=" << gerber_scale << endl;
        }

        sink = new SimpleGerberOutput(*out_f, only_polys, 4, precision, gerber_scale, {0,0}, args["flip_gerber_polarity"]);
        //cerr << "  * Gerber sink " << endl;

    } else if (fmt == "s-exp" || fmt == "sexp" || fmt == "kicad") {
        if (!args["sexp_mod_name"]) {
            cerr << "Error: --sexp-mod-name must be given for sexp export" << endl;
            return EXIT_FAILURE;
        }

        sink = new KicadSexpOutput(*out_f, args["sexp_mod_name"], sexp_layer, only_polys);
        force_flatten = true;
        is_sexp = true;
        //cerr << "  * KiCAD SExp sink " << endl;

    } else {
        cerr << "Error: Unknown output format \"" << fmt << "\"" << endl;
        return EXIT_FAILURE;
    }

    PolygonSink *top_sink = sink;

    if (args["dilate"]) {
        dilater = new Dilater(*top_sink, args["dilate"].as<double>());
        top_sink = dilater;
        //cerr << "  * Dilater " << endl;
    }

    if (args["flatten"] || (force_flatten && !args["no_flatten"])) {
        flattener = new Flattener(*top_sink);
        top_sink = flattener;
        //cerr << "  * Flattener " << endl;
    }

    /* Because the C++ stdlib is bullshit */
    auto id_match = [](string in, vector<string> &out) {
        stringstream  ss(in);
        while (getline(ss, out.emplace_back(), ',')) {
        }
        out.pop_back();
    };

    IDElementSelector sel;
    if (args["only_groups"])
        id_match(args["only_groups"], sel.include);
    if (args["exclude_groups"])
        id_match(args["exclude_groups"], sel.exclude);
    if (is_sexp && sexp_layer == "auto") {
        sel.layers = &gerbolyze::kicad_default_layers;
    }

    string vectorizer = args["vectorizer"] ? args["vectorizer"].as<string>() : "poisson-disc";
    /* Check argument */
    ImageVectorizer *vec = makeVectorizer(vectorizer);
    if (!vec) {
        cerr << "Error: Unknown vectorizer \"" << vectorizer << "\"." << endl;
        argagg::fmt_ostream fmt(cerr);
        fmt << usage.str() << argparser;
        return EXIT_FAILURE;
    }
    delete vec;

    double min_feature_size = args["min_feature_size"].as<double>(0.1); /* mm */
    double geometric_tolerance = args["geometric_tolerance"].as<double>(0.01); /* mm */
    double stroke_width_cutoff = args["stroke_width_cutoff"].as<double>(0.01); /* mm */
    double drill_test_polsby_popper_tolerance = args["drill_test_polsby_popper_tolerance"].as<double>(0.1);
    double aperture_rect_test_tolerance = args["aperture_rect_test_tolerance"].as<double>(0.1);
    double aperture_circle_test_tolerance = args["aperture_circle_test_tolerance"].as<double>(0.1);

    string ending = "";
    auto idx = in_f_name.rfind(".");
    if (idx != string::npos) {
        ending = in_f_name.substr(idx);
        transform(ending.begin(), ending.end(), ending.begin(), [](unsigned char c){ return std::tolower(c); }); /* c++ yeah */
    }
    
    string barf =  temp_file_path(".svg");
    string frob = temp_file_path(".svg");

    bool is_svg = args["force_svg"] || (ending == ".svg" && !args["force_png"]);
    if (!is_svg) {
        //cerr << "writing bitmap into svg" << endl; 
        if (!args["size"]) {
            cerr << "Error: --size must be given when using bitmap input." << endl;
            return EXIT_FAILURE;
        }

        string sz = args["size"].as<string>();
        auto pos = sz.find_first_of("x*,");
        if (pos == string::npos) {
            cerr << "Error: --size must be of form 12.34x56.78" << endl;
            return EXIT_FAILURE;
        }

        string x_str = sz.substr(0, pos);
        string y_str = sz.substr(pos+1);

        double width = std::strtod(x_str.c_str(), nullptr);
        double height = std::strtod(y_str.c_str(), nullptr);

        if (width < 1 || height < 1) {
            cerr << "Error: --size must be of form 12.34x56.78 and values must be positive floating-point numbers in mm" << endl;
            return EXIT_FAILURE;
        }

        ofstream svg(barf.c_str());

        svg << "<svg width=\"" << width << "mm\" height=\"" << height << "mm\" viewBox=\"0 0 "
            << width << " " << height << "\" "
            << "xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">" << endl;

        string par_attr = "none";
        if (args["preserve_aspect_ratio"]) {
            string aspect_ratio = args["preserve_aspect_ratio"].as<string>();
            if (aspect_ratio == "meet") {
                par_attr = "xMidYMid meet";
            } else if (aspect_ratio == "slice") {
                par_attr = "xMidYMid slice";
            } else {
                par_attr = aspect_ratio;
            }
        }
        svg << "<image width=\"" << width << "\" height=\"" << height << "\" x=\"0\" y=\"0\" preserveAspectRatio=\""
            << par_attr << "\" xlink:href=\"data:image/png;base64,";
        
        /* c++ has the best hacks */
        std::ostringstream sstr;
        sstr << in_f->rdbuf();
        string le_data = sstr.str();

        svg << base64_encode(le_data);
        svg << "\"/>" << endl;

        svg << "</svg>" << endl;
        svg.close();

    } else { /* svg file */
        //cerr << "copying svg input into temp svg" << endl; 

        /* c++ has the best hacks */
        std::ostringstream sstr;
        sstr << in_f->rdbuf();

        ofstream tmp_out(barf.c_str());
        tmp_out << sstr.str();
        tmp_out.close();

    }

    if (args["skip_usvg"]) {
        frob = barf;

    } else {
#ifndef NOFORK
        //cerr << "calling usvg on " << barf << " and " << frob << endl; 
        vector<string> command_line;

        string options[] = {
            "usvg-dpi",
            "usvg-font-family",
            "usvg-font-size",
            "usvg-serif-family",
            "usvg-sans-serif-family",
            "usvg-cursive-family",
            "usvg-fantasy-family",
            "usvg-monospace-family",
            "usvg-use-font-file",
            "usvg-use-fonts-dir",
        };

        for (string &opt : options) {
            if (args[opt.c_str()]) {
                command_line.push_back("--" + opt.substr(5));
                command_line.push_back(args[opt.c_str()]);
            }
        }

        if (args["usvg-skip-system-fonts"]) {
            command_line.push_back("--skip-system-fonts");
        }

        command_line.push_back(barf);
        command_line.push_back(frob);
        
        if (run_cargo_command("usvg", command_line, "USVG")) {
            return EXIT_FAILURE;
        }
#else
        cerr << "Error: The caller of svg-flatten (you?) must use --no-usvg and run usvg externally since wasi does not yet support fork/exec." << endl;
        return EXIT_FAILURE;
#endif
    }

    VectorizerSelectorizer vec_sel(vectorizer, args["vectorizer_map"] ? args["vectorizer_map"].as<string>() : "");
    bool flip_svg_colors = args["flip_svg_color_interpretation"];
    bool pattern_complete_tiles_only = args["pattern_complete_tiles_only"];
    bool use_apertures_for_patterns = args["use_apertures_for_patterns"];
    bool do_gerber_interpolation = !args["no_stroke_interpolation"];

    RenderSettings rset {
        min_feature_size,
        geometric_tolerance,
        stroke_width_cutoff,
        drill_test_polsby_popper_tolerance,
        aperture_circle_test_tolerance,
        aperture_rect_test_tolerance,
        vec_sel,
        outline_mode,
        flip_svg_colors,
        pattern_complete_tiles_only,
        use_apertures_for_patterns,
        do_gerber_interpolation,
    };

    SVGDocument doc;
    //cerr << "Loading temporary file " << frob << endl;

    double scale = args["scale"].as<double>(1.0);
    if (scale != 1.0) {
        cerr << "Info: Loading scaled input @scale=" << scale << endl;
    }

    ifstream load_f(frob);
    if (!doc.load(load_f, scale)) {
        cerr <<  "Error loading input file \"" << in_f_name << "\", exiting." << endl;
        return EXIT_FAILURE;
    }

    /*
    cerr << "Selectors:" << endl;
    for (auto &elem : sel.include) {
        cerr << " + " << elem << endl;
    }
    for (auto &elem : sel.exclude) {
        cerr << " - " << elem << endl;
    }
    */
    doc.render(rset, *top_sink, sel);

    remove(frob.c_str());
    remove(barf.c_str());

    if (flattener) {
        delete flattener;
    }
    if (dilater) {
        delete dilater;
    }
    if (sink) {
        delete sink;
    }
    return EXIT_SUCCESS;
}

