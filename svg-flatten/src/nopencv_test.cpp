
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <filesystem>

#include "nopencv.hpp"

#include <subprocess.h>
#include <minunit.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace gerbolyze;
using namespace gerbolyze::nopencv;

char msg[1024];

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

    const Polygon expected_polys[3] = {
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
    gerbolyze::nopencv::find_blobs(test_img, [&invocation_count, &expected_polarities, &expected_polys](Polygon poly, ContourPolarity pol) {
            invocation_count += 1;
            mu_assert((invocation_count <= 3), "Too many contours returned"); 

            mu_assert(poly.size() > 0, "Empty contour returned");
            mu_assert_int_eq(pol, expected_polarities[invocation_count-1]);

            d2p last;
            bool first = true;
            Polygon exp = expected_polys[invocation_count-1];
            //cout << "poly: ";
            for (d2p &p : poly) {
                //cout << "(" << p[0] << ", " << p[1] << "), ";
                if (!first) {
                    mu_assert((fabs(p[0] - last[0]) + fabs(p[1] - last[1]) == 1), "Subsequent contour points have distance other than one");
                    mu_assert(find(exp.begin(), exp.end(), p) != exp.end(), "Got unexpected contour point");
                }
                last = p;
            }
            //cout << endl;
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

MU_TEST(test_round_trip) {
    int x, y;
    uint8_t *data = stbi_load("testdata/paper-example.png", &x, &y, nullptr, 1);
    Image32 ref_img(x, y);
    for (int cy=0; cy<y; cy++) {
        for (int cx=0; cx<x; cx++) {
            ref_img.at(cx, cy) = data[cy*x + cx];
        }
    }
    stbi_image_free(data);
    Image32 ref_img_copy(ref_img);

    filesystem::path tmp_svg = { filesystem::temp_directory_path() /= (std::tmpnam(nullptr) + string(".svg")) };
    filesystem::path tmp_png = { filesystem::temp_directory_path() /= (std::tmpnam(nullptr) + string(".png")) };
    ofstream svg(tmp_svg.c_str());

    svg << "<svg width=\"" << x << "px\" height=\"" << y << "px\" viewBox=\"0 0 "
        << x << " " << y << "\" "
        << "xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">" << endl;
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"black\">" << endl;

    gerbolyze::nopencv::find_blobs(ref_img, [&svg](Polygon poly, ContourPolarity pol) {
        mu_assert(poly.size() > 0, "Empty contour returned");
        mu_assert(poly.size() > 2, "Contour has less than three points, no area");
        mu_assert(pol == CP_CONTOUR || pol == CP_HOLE, "Contour has invalid polarity");

        svg << "<path fill=\"" << (pol == CP_HOLE ? "black" : "white") << "\" d=\"";
        svg << "M " << poly[0][0] << " " << poly[0][1];
        for (size_t i=1; i<poly.size(); i++) {
            svg << " L " << poly[i][0] << " " << poly[i][1];
        }
        svg << " Z\">" << endl;
    });
    svg << "</svg>" << endl;
    svg.close();

    const char *command_line[] = {"resvg", tmp_svg.c_str(), tmp_png.c_str()};
    struct subprocess_s subprocess;
    int rc = subprocess_create(command_line, subprocess_option_inherit_environment, &subprocess);
    mu_assert_int_eq(rc, 0);

    int resvg_rc = -1;
    rc = subprocess_join(&subprocess, &resvg_rc);
    mu_assert_int_eq(rc, 0);
    mu_assert_int_eq(resvg_rc, 0);

    rc = subprocess_destroy(&subprocess);
    mu_assert_int_eq(rc, 0);

    int out_x, out_y;
    uint8_t *out_data = stbi_load(tmp_png.c_str(), &out_x, &out_y, nullptr, 1);
    mu_assert_int_eq(out_x, x);
    mu_assert_int_eq(out_y, y);

    for (int cy=0; cy<y; cy++) {
        for (int cx=0; cx<x; cx++) {
            int actual = out_data[cy*x + cx];
            int expected = ref_img_copy.at(x, y);
            if (actual != expected) {
                snprintf(msg, sizeof(msg), "Result does not match input @(%d, %d): %d != %d\n", cx, cy, actual, expected);
                mu_fail(msg);
            }
        }
    }
    stbi_image_free(out_data);
}

MU_TEST_SUITE(nopencv_contours_suite) {
    MU_RUN_TEST(test_complex_example_from_paper);
//    MU_RUN_TEST(test_round_trip);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    MU_RUN_SUITE(nopencv_contours_suite);
    MU_REPORT();
    return MU_EXIT_CODE;
}
