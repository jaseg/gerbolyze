
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <argagg.hpp>
#include <gerbolyze.hpp>

using argagg::parser_results;
using argagg::parser;
using namespace std;
using namespace gerbolyze;

int main(int argc, char **argv) {
    parser argparser {{
            {"help", {"-h", "--help"},
                "Print help and exit",
                0},
            {"version", {"-v", "--version"},
                "Print version and exit",
                0},
            {"ofmt", {"-o", "--output-format"},
                "Output format. Supported: gerber, svg, s-exp (KiCAD S-Expression)",
                1},
            {"flatten", {"-f", "--flatten"},
                "Flatten output so it only consists of non-overlapping white polygons. This perform composition at the vector level. Potentially slow.",
                0},
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
    try {
        args = argparser.parse(argc, argv);
    } catch (const std::exception& e) {
        argagg::fmt_ostream fmt(cerr);
        fmt << usage.str() << argparser << '\n'
            << "Encountered exception while parsing arguments: " << e.what()
            << '\n';
        return EXIT_FAILURE;
    }

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


    SVGDocument doc;
    if (!doc.load(*in_f)) {
        cerr <<  "Error loading input file \"" << in_f_name << "\", exiting." << endl;
        return EXIT_FAILURE;
    }

    SimpleGerberOutput out(*out_f);
    doc.render(out);
    return EXIT_SUCCESS;
}
