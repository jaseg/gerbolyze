
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <filesystem>

#include "nopencv.hpp"

#include <subprocess.h>
#include <minunit.h>

#include "stb_image.h"

using namespace gerbolyze;
using namespace gerbolyze::nopencv;

char msg[1024];

class TempfileHack {
public:
    TempfileHack(const string ext) : m_path { filesystem::temp_directory_path() / (std::tmpnam(nullptr) + ext) } {}
    ~TempfileHack() { remove(m_path); }

    const char *c_str() { return m_path.c_str(); }

private:
    filesystem::path m_path;
};

class SVGPolyRenderer {
public:
    SVGPolyRenderer(const char *fn, int width_px, int height_px)
        : m_svg(fn) {
        m_svg << "<svg width=\"" << width_px << "px\" height=\"" << height_px << "px\" viewBox=\"0 0 "
            << width_px << " " << height_px << "\" "
            << "xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">" << endl;
        m_svg << "<rect width=\"100%\" height=\"100%\" fill=\"black\"/>" << endl;
    }

    ContourCallback callback() {
        return [this](Polygon_i &poly, ContourPolarity pol) {
            mu_assert(poly.size() > 0, "Empty contour returned");
            mu_assert(poly.size() > 2, "Contour has less than three points, no area");
            mu_assert(pol == CP_CONTOUR || pol == CP_HOLE, "Contour has invalid polarity");

            m_svg << "<path fill=\"" << ((pol == CP_HOLE) ? "black" : "white") << "\" d=\"";
            m_svg << "M " << poly[0][0] << " " << poly[0][1];
            for (size_t i=1; i<poly.size(); i++) {
                m_svg << " L " << poly[i][0] << " " << poly[i][1];
            }
            m_svg << " Z\"/>" << endl;
        };
    }

    void close() {
        m_svg << "</svg>" << endl;
        m_svg.close();
    }

private:
    ofstream m_svg;
};

MU_TEST(test_complex_example_from_paper) {
    int32_t img_data[6*9] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 1, 1, 1, 1, 1, 1, 1, 0,
        0, 1, 0, 0, 1, 0, 0, 1, 0,
        0, 1, 0, 0, 1, 0, 0, 1, 0,
        0, 1, 1, 1, 1, 1, 1, 1, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    Image32 test_img(9, 6, static_cast<int*>(img_data));

    const Polygon_i expected_polys[3] = {
        {
            {1,1}, {1,2}, {1,3}, {1,4}, {1,5},
            {2,5}, {3,5}, {4,5}, {5,5}, {6,5}, {7,5}, {8,5},
            {8,4}, {8,3}, {8,2}, {8,1},
            {7,1}, {6,1}, {5,1}, {4,1}, {3,1}, {2,1}
        },
        {
            {2,2}, {2,3}, {2,4},
            {3,4}, {4,4},
            {4,3}, {4,2},
            {3,2}
        },
        {
            {5,2}, {5,3}, {5,4},
            {6,4}, {7,4},
            {7,3}, {7,2},
            {6,2}
        }
    };

    const ContourPolarity expected_polarities[3] = {CP_CONTOUR, CP_HOLE, CP_HOLE};
    
    int invocation_count = 0;
    gerbolyze::nopencv::find_blobs(test_img, [&invocation_count, &expected_polarities, &expected_polys](Polygon_i &poly, ContourPolarity pol) {
            invocation_count += 1;
            mu_assert((invocation_count <= 3), "Too many contours returned"); 

            mu_assert(poly.size() > 0, "Empty contour returned");
            mu_assert_int_eq(pol, expected_polarities[invocation_count-1]);

            i2p last;
            bool first = true;
            Polygon_i exp = expected_polys[invocation_count-1];
            for (auto &p : poly) {
                if (!first) {
                    mu_assert((fabs(p[0] - last[0]) + fabs(p[1] - last[1]) == 1), "Subsequent contour points have distance other than one");
                    mu_assert(find(exp.begin(), exp.end(), p) != exp.end(), "Got unexpected contour point");
                }
                last = p;
            }
        });
    mu_assert_int_eq(3, invocation_count);

    int32_t tpl[6*9] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 2, 2, 2, 2, 2, 2,-2, 0,
        0,-3, 0, 0,-4, 0, 0,-2, 0,
        0,-3, 0, 0,-4, 0, 0,-2, 0,
        0, 2, 2, 2, 2, 2, 2,-2, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0,
    };


    for (int y=0; y<6; y++) {
        for (int x=0; x<9; x++) {
            int a = test_img.at(x, y), b = tpl[y*9+x];
            if (a != b) {
                cout << "Result:" << endl;
                cout << "    ";
                for (int x=0; x<9; x++) {
                    cout << x << "  ";
                }
                cout << endl;
                cout << "    ";
                for (int x=0; x<9; x++) {
                    cout << "---";
                }
                cout << endl;
                for (int y=0; y<6; y++) {
                    cout << y << " | ";
                    for (int x=0; x<9; x++) {
                        cout << setfill(' ') << setw(2) << test_img.at(x, y) << " ";
                    }
                    cout << endl;
                }

                snprintf(msg, sizeof(msg), "Result does not match template @(%d, %d): %d != %d\n", x, y, a, b);
                mu_fail(msg);
            }
        }
    }
}

int render_svg(const char *in_svg, const char *out_png) {
    const char *command_line[] = {"resvg", in_svg, out_png, nullptr};
    struct subprocess_s subprocess;
    int rc = subprocess_create(command_line, subprocess_option_inherit_environment, &subprocess);
    if (rc)
        return rc;

    int resvg_rc = -1;
    rc = subprocess_join(&subprocess, &resvg_rc);
    if (rc)
        return rc;
    if (resvg_rc)
        return -resvg_rc;

    rc = subprocess_destroy(&subprocess);
    if (rc)
        return rc;

    return 0;
}

static void testdata_roundtrip(const char *fn) {
    Image32 ref_img;
    mu_assert(ref_img.load(fn), "Input image failed to load");
    ref_img.binarize();
    Image32 ref_img_copy(ref_img);

    TempfileHack tmp_svg(".svg");
    TempfileHack tmp_png(".png");

    SVGPolyRenderer ctx(tmp_svg.c_str(), ref_img.cols(), ref_img.rows());
    gerbolyze::nopencv::find_blobs(ref_img, ctx.callback());
    ctx.close();

    mu_assert_int_eq(0, render_svg(tmp_svg.c_str(), tmp_png.c_str()));

    Image32 out_img;
    mu_assert(out_img.load(tmp_png.c_str()), "Output image failed to load");
    out_img.binarize();

    mu_assert_int_eq(ref_img.cols(), out_img.cols());
    mu_assert_int_eq(ref_img.rows(), out_img.rows());

    for (int y=0; y<out_img.rows(); y++) {
        for (int x=0; x<out_img.cols(); x++) {
            if (out_img.at(x, y) != ref_img_copy.at(x, y)) {
                snprintf(msg, sizeof(msg), "%s: Result does not match input @(%d, %d): %d != %d\n", fn, x, y, out_img.at(x, y), ref_img_copy.at(x, y));
                mu_fail(msg);
            }
        }
    }
}

MU_TEST(test_round_trip_blank)              { testdata_roundtrip("testdata/blank.png"); }
MU_TEST(test_round_trip_white)              { testdata_roundtrip("testdata/white.png"); }
MU_TEST(test_round_trip_blob_border_w)      { testdata_roundtrip("testdata/blob-border-w.png"); }
MU_TEST(test_round_trip_blobs_borders)      { testdata_roundtrip("testdata/blobs-borders.png"); }
MU_TEST(test_round_trip_blobs_corners)      { testdata_roundtrip("testdata/blobs-corners.png"); }
MU_TEST(test_round_trip_blobs_crossing)     { testdata_roundtrip("testdata/blobs-crossing.png"); }
MU_TEST(test_round_trip_cross)              { testdata_roundtrip("testdata/cross.png"); }
MU_TEST(test_round_trip_letter_e)           { testdata_roundtrip("testdata/letter-e.png"); }
MU_TEST(test_round_trip_paper_example)      { testdata_roundtrip("testdata/paper-example.png"); }
MU_TEST(test_round_trip_paper_example_inv)  { testdata_roundtrip("testdata/paper-example-inv.png"); }
MU_TEST(test_round_trip_single_px)          { testdata_roundtrip("testdata/single-px.png"); }
MU_TEST(test_round_trip_single_px_inv)      { testdata_roundtrip("testdata/single-px-inv.png"); }
MU_TEST(test_round_trip_two_blobs)          { testdata_roundtrip("testdata/two-blobs.png"); }
MU_TEST(test_round_trip_two_px)             { testdata_roundtrip("testdata/two-px.png"); }
MU_TEST(test_round_trip_two_px_inv)         { testdata_roundtrip("testdata/two-px-inv.png"); }

static void test_polygon_area(const char *fn) {
    cerr << endl << "poly area test " << fn << endl;
    Image32 ref_img;
    mu_assert(ref_img.load(fn), "Input image failed to load");
    ref_img.binarize();

    int white_px_count = 0;
    int black_px_count = 0;
    for (int y=0; y<ref_img.rows(); y++) {
        for (int x=0; x<ref_img.cols(); x++) {
            if (ref_img.at(x, y)) {
                white_px_count += 1;
            } else {
                black_px_count += 1;
            }
        }
    }

    double pos_sum = 0.0;
    double neg_sum = 0.0;
    gerbolyze::nopencv::find_blobs(ref_img, [fn, white_px_count, black_px_count, &pos_sum, &neg_sum](Polygon_i& poly, ContourPolarity pol) {
            double area = polygon_area(poly);
            cerr << endl << fn << ": " << area << pos_sum << " / " << neg_sum << " -- " << white_px_count << " / " << black_px_count << endl;
            mu_assert(fabs(area) > 0.99, "Polygon smaller than a single pixel");
            mu_assert((pol == CP_CONTOUR) == (area >= 0), "Polygon area has incorrect sign");

            if (area > 0) {
                pos_sum += area;
            } else {
                neg_sum -= area;
            }
        });

    mu_assert(pos_sum - white_px_count < 0.01, "Calculated area outside tolerance");
    mu_assert(neg_sum - black_px_count < 0.01, "Calculated area outside tolerance");
    cerr << endl << "poly area test " << fn << " done" << endl;
}

MU_TEST(test_polygon_area_blank)              { test_polygon_area("testdata/blank.png"); }
MU_TEST(test_polygon_area_white)              { test_polygon_area("testdata/white.png"); }
MU_TEST(test_polygon_area_blob_border_w)      { test_polygon_area("testdata/blob-border-w.png"); }
MU_TEST(test_polygon_area_blobs_borders)      { test_polygon_area("testdata/blobs-borders.png"); }
MU_TEST(test_polygon_area_blobs_corners)      { test_polygon_area("testdata/blobs-corners.png"); }
MU_TEST(test_polygon_area_blobs_crossing)     { test_polygon_area("testdata/blobs-crossing.png"); }
MU_TEST(test_polygon_area_cross)              { test_polygon_area("testdata/cross.png"); }
MU_TEST(test_polygon_area_letter_e)           { test_polygon_area("testdata/letter-e.png"); }
MU_TEST(test_polygon_area_paper_example)      { test_polygon_area("testdata/paper-example.png"); }
MU_TEST(test_polygon_area_paper_example_inv)  { test_polygon_area("testdata/paper-example-inv.png"); }
MU_TEST(test_polygon_area_single_px)          { test_polygon_area("testdata/single-px.png"); }
MU_TEST(test_polygon_area_single_px_inv)      { test_polygon_area("testdata/single-px-inv.png"); }
MU_TEST(test_polygon_area_two_blobs)          { test_polygon_area("testdata/two-blobs.png"); }
MU_TEST(test_polygon_area_two_px)             { test_polygon_area("testdata/two-px.png"); }
MU_TEST(test_polygon_area_two_px_inv)         { test_polygon_area("testdata/two-px-inv.png"); }

static void chain_approx_test(const char *fn) {
    //cout << endl << "Testing \"" << fn << "\"" << endl;
    Image32 ref_img;
    mu_assert(ref_img.load(fn), "Input image failed to load");
    ref_img.binarize();
    Image32 ref_img_copy(ref_img);

    TempfileHack tmp_svg(".svg");
    TempfileHack tmp_png(".png");

    SVGPolyRenderer ctx(tmp_svg.c_str(), ref_img.cols(), ref_img.rows());
    gerbolyze::nopencv::find_blobs(ref_img, simplify_contours_teh_chin(ctx.callback()));
    ctx.close();

    mu_assert_int_eq(0, render_svg(tmp_svg.c_str(), tmp_png.c_str()));

    Image32 out_img;
    mu_assert(out_img.load(tmp_png.c_str()), "Output image failed to load");
    mu_assert_int_eq(ref_img.rows(), out_img.rows());
    mu_assert_int_eq(ref_img.cols(), out_img.cols());

    double max_abs_deviation = 0;
    double rms_sum = 0; 
    double mean_sum = 0; 
    for (int y=0; y<out_img.rows(); y++) {
        for (int x=0; x<out_img.cols(); x++) {
            double delta = fabs((double)out_img.at(x, y)/255.0 - (double)ref_img_copy.at(x, y));
            max_abs_deviation = fmax(max_abs_deviation, delta);
            rms_sum += delta*delta;
            mean_sum += delta;
        }
    }

    rms_sum = sqrt(rms_sum / out_img.size());
    mean_sum /= out_img.size();
    if (rms_sum > 0.5) {
        snprintf(msg, sizeof(msg), "%s: Chain approximation RMS error is above threshold: %.3f > 0.5\n", fn, rms_sum);
        mu_fail(msg);
    }
    if (mean_sum > 0.1) {
        snprintf(msg, sizeof(msg), "%s: Chain approximation mean error is above threshold: %.3f > 0.1\n", fn, mean_sum);
        mu_fail(msg);
    }
    //mu_assert(max_abs_deviation < 0.5, "Maximum chain approximation error is above threshold");
}


MU_TEST(chain_approx_test_chromosome)         { chain_approx_test("testdata/chain-approx-teh-chin-chromosome.png"); }
MU_TEST(chain_approx_test_blank)              { chain_approx_test("testdata/blank.png"); }
MU_TEST(chain_approx_test_white)              { chain_approx_test("testdata/white.png"); }
MU_TEST(chain_approx_test_blob_border_w)      { chain_approx_test("testdata/blob-border-w.png"); }
MU_TEST(chain_approx_test_blobs_borders)      { chain_approx_test("testdata/blobs-borders.png"); }
MU_TEST(chain_approx_test_blobs_corners)      { chain_approx_test("testdata/blobs-corners.png"); }
MU_TEST(chain_approx_test_blobs_crossing)     { chain_approx_test("testdata/blobs-crossing.png"); }
MU_TEST(chain_approx_test_cross)              { chain_approx_test("testdata/cross.png"); }
MU_TEST(chain_approx_test_letter_e)           { chain_approx_test("testdata/letter-e.png"); }
MU_TEST(chain_approx_test_paper_example)      { chain_approx_test("testdata/paper-example.png"); }
MU_TEST(chain_approx_test_paper_example_inv)  { chain_approx_test("testdata/paper-example-inv.png"); }
MU_TEST(chain_approx_test_single_px)          { chain_approx_test("testdata/single-px.png"); }
MU_TEST(chain_approx_test_single_px_inv)      { chain_approx_test("testdata/single-px-inv.png"); }
MU_TEST(chain_approx_test_two_blobs)          { chain_approx_test("testdata/two-blobs.png"); }
MU_TEST(chain_approx_test_two_px)             { chain_approx_test("testdata/two-px.png"); }
MU_TEST(chain_approx_test_two_px_inv)         { chain_approx_test("testdata/two-px-inv.png"); }


MU_TEST_SUITE(nopencv_contours_suite) {
    /*
    MU_RUN_TEST(test_complex_example_from_paper);
    MU_RUN_TEST(test_round_trip_blank);
    MU_RUN_TEST(test_round_trip_white);
    MU_RUN_TEST(test_round_trip_blob_border_w);
    MU_RUN_TEST(test_round_trip_blobs_borders);
    MU_RUN_TEST(test_round_trip_blobs_corners);
    MU_RUN_TEST(test_round_trip_blobs_crossing);
    MU_RUN_TEST(test_round_trip_cross);
    MU_RUN_TEST(test_round_trip_letter_e);
    MU_RUN_TEST(test_round_trip_paper_example);
    MU_RUN_TEST(test_round_trip_paper_example_inv);
    MU_RUN_TEST(test_round_trip_single_px);
    MU_RUN_TEST(test_round_trip_single_px_inv);
    MU_RUN_TEST(test_round_trip_two_blobs);
    MU_RUN_TEST(test_round_trip_two_px);
    MU_RUN_TEST(test_round_trip_two_px_inv);
    MU_RUN_TEST(chain_approx_test_chromosome);
    MU_RUN_TEST(chain_approx_test_blank);
    MU_RUN_TEST(chain_approx_test_white);
    MU_RUN_TEST(chain_approx_test_blob_border_w);
    MU_RUN_TEST(chain_approx_test_blobs_borders);
    MU_RUN_TEST(chain_approx_test_blobs_corners);
    MU_RUN_TEST(chain_approx_test_blobs_crossing);
    MU_RUN_TEST(chain_approx_test_cross);
    MU_RUN_TEST(chain_approx_test_letter_e);
    MU_RUN_TEST(chain_approx_test_paper_example);
    MU_RUN_TEST(chain_approx_test_paper_example_inv);
    MU_RUN_TEST(chain_approx_test_single_px);
    MU_RUN_TEST(chain_approx_test_single_px_inv);
    MU_RUN_TEST(chain_approx_test_two_blobs);
    MU_RUN_TEST(chain_approx_test_two_px);
    MU_RUN_TEST(chain_approx_test_two_px_inv);
    */
    MU_RUN_TEST(test_polygon_area_blank);
    MU_RUN_TEST(test_polygon_area_white);
    MU_RUN_TEST(test_polygon_area_blob_border_w);
    MU_RUN_TEST(test_polygon_area_blobs_borders);
    MU_RUN_TEST(test_polygon_area_blobs_corners);
    MU_RUN_TEST(test_polygon_area_blobs_crossing);
    MU_RUN_TEST(test_polygon_area_cross);
    MU_RUN_TEST(test_polygon_area_letter_e);
    MU_RUN_TEST(test_polygon_area_paper_example);
    MU_RUN_TEST(test_polygon_area_paper_example_inv);
    MU_RUN_TEST(test_polygon_area_single_px);
    MU_RUN_TEST(test_polygon_area_single_px_inv);
    MU_RUN_TEST(test_polygon_area_two_blobs);
    MU_RUN_TEST(test_polygon_area_two_px);
    MU_RUN_TEST(test_polygon_area_two_px_inv);
};

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    MU_RUN_SUITE(nopencv_contours_suite);
    MU_REPORT();
    return MU_EXIT_CODE;
}
