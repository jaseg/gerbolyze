
#include <iostream>
#include <iomanip>

#include "nopencv.hpp"

using namespace gerbolyze;
using namespace gerbolyze::nopencv;

/* directions:
 *        0
 *   7         1
 *        ^   
 *        |
 * 6 <--- X ---> 2
 *        |
 *        v
 *   5         3
 *        4
 *
 */
enum Direction {
    D_N,
    D_NE,
    D_E,
    D_SE,
    D_S,
    D_SW,
    D_W,
    D_NW
};

//const char * const dir_str[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };

static struct {
    int x;
    int y;
} dir_to_coords[8] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};

static Direction flip_direction[8] = {
    D_S,  /* 0 */
    D_SW, /* 1 */
    D_W,  /* 2 */
    D_NW, /* 3 */
    D_N,  /* 4 */
    D_NE, /* 5 */
    D_E,  /* 6 */
    D_SE  /* 7 */
};

static void follow(gerbolyze::nopencv::Image32 &img, int start_x, int start_y, Direction initial_direction, int nbd, int connectivity, Polygon_i &poly) {

    //cerr << "follow " << start_x << " " << start_y << " | dir=" << dir_str[initial_direction] << " nbd=" << nbd << " conn=" << connectivity << endl;
    int dir_inc = (connectivity == 4) ? 2 : 1;

    int probe_x, probe_y;

    /* homing run: find starting point for algorithm steps below. */
    bool found = false;
    int k;
    for (k=initial_direction; k<initial_direction+8; k += dir_inc) {
        probe_x = start_x + dir_to_coords[k % 8].x;
        probe_y = start_y + dir_to_coords[k % 8].y;

        if (img.at_default(probe_x, probe_y) != 0) {
            found = true;
            break;
        }
    }

    if (!found) { /* No nonzero pixels found. This is a single-pixel contour */
        img.at(start_x, start_y) = nbd;
        poly.emplace_back(i2p{start_x,   start_y});
        poly.emplace_back(i2p{start_x+1, start_y});
        poly.emplace_back(i2p{start_x+1, start_y+1});
        poly.emplace_back(i2p{start_x,   start_y+1});

        return;
    }

    /* starting point found. */
    int current_direction = k % 8;
    int start_direction = current_direction;
    int center_x = start_x, center_y = start_y;
    //cerr << "  init: " << center_x << " " << center_y << " / " << dir_str[current_direction] << endl;

    do {
        bool flag = false;
        for (k = current_direction + 8 - dir_inc; k >= current_direction; k -= dir_inc) {
            probe_x = center_x + dir_to_coords[k % 8].x;
            probe_y = center_y + dir_to_coords[k % 8].y;
            if (k%8 == D_E)
                flag = true;

            if (img.at_default(probe_x, probe_y) != 0) {
                break;
            }
        }

        int set_val = 0;
        if (flag && img.at_default(center_x+1, center_y) == 0) {
            img.at(center_x, center_y) = -nbd;
            set_val = -nbd;
        } else if (img.at(center_x, center_y) == 1) {
            img.at(center_x, center_y) = nbd;
            set_val = nbd;
        }

        for (int l = (current_direction + 8 - 2 + 1) / 2 * 2; l > k; l -= dir_inc) {
            switch (l%8) {
                case 0: poly.emplace_back(i2p{center_x,   center_y}); break;
                case 2: poly.emplace_back(i2p{center_x+1, center_y}); break;
                case 4: poly.emplace_back(i2p{center_x+1, center_y+1}); break;
                case 6: poly.emplace_back(i2p{center_x,   center_y+1}); break;
            }
        }

        center_x = probe_x;
        center_y = probe_y;
        current_direction = flip_direction[k % 8];

        //cerr << "  " << center_x << " " << center_y << " / " << dir_str[current_direction] << " -> " << set_val << endl;
    } while (center_x != start_x || center_y != start_y || current_direction != start_direction);
}


void gerbolyze::nopencv::find_blobs(gerbolyze::nopencv::Image32 &img, gerbolyze::nopencv::ContourCallback cb) {
    /* Implementation of the hierarchical contour finding algorithm from Suzuki and Abe, 1983: Topological Structural
     * Analysis of Digitized Binary Images by Border Following
     *
     * Written with these two resources as reference:
     *     https://theailearner.com/tag/suzuki-contour-algorithm-opencv/
     *     https://github.com/FreshJesh5/Suzuki-Algorithm/blob/master/contoursv1/contoursv1.cpp
     */
    int nbd = 1;
    Polygon_i poly;
    for (int y=0; y<img.rows(); y++) {
        for (int x=0; x<img.cols(); x++) {
            int val_xy = img.at(x, y);
            /* Note: outer borders are followed with 8-connectivity, hole borders with 4-connectivity. This prevents
             * incorrect results in this case:
             *
             *    1   1   1 | 0   0   0
             *              |
             *    1   1   1 | 0   0   0
             *    ----------+---------- <== Here
             *    0   0   0 | 1   1   1
             *              |
             *    0   0   0 | 1   1   1
             */
            if (img.at_default(x-1, y) == 0 && val_xy == 1) { /* outer border starting point */
                nbd += 1;
                follow(img, x, y, D_W, nbd, 8, poly);
                cb(poly, CP_CONTOUR);
                poly.clear();

            } else if (val_xy >= 1 && img.at_default(x+1, y) == 0) { /* hole border starting point */
                nbd += 1;
                follow(img, x, y, D_E, nbd, 8, poly); /* FIXME should be 4? */
                cb(poly, CP_HOLE);
                poly.clear();
            }
        }
    }
}

static size_t region_of_support(Polygon_i poly, size_t i) { 
    double x0 = poly[i][0], y0 = poly[i][1];
    size_t sz = poly.size();
    double last_l = 0;
    double last_r = 0;
    size_t k;
    //cerr << "d: ";
    for (k=1; k<(sz+1)/2; k++) {
        size_t idx1 = (i + k) % sz;
        size_t idx2 = (i + sz - k) % sz;
        double x1 = poly[idx1][0], y1 = poly[idx1][1], x2 = poly[idx2][0], y2 = poly[idx2][1];
        double l = sqrt(pow(x2-x1, 2) + pow(y2-y1, 2));
        /* https://en.wikipedia.org/wiki/Distance_from_a_point_to_a_line
         * TODO: Check whether distance-to-line is an ok implementation here, the paper asks for distance to chord.
         */
        double d = ((x2-x1)*(y1-y0) - (x1-x0)*(y2-y1)) / sqrt(pow(x2-x1, 2) + pow(y2-y1, 2));
        //cerr << d << " ";
        double r = d/l;

        bool cond_a = l < last_l;
        bool cond_b = ((d > 0) && (r < last_r)) || ((d < 0) && (r > last_r));

        if (k > 2 && (cond_a || cond_b))
            break;

        last_l = l;
        last_r = r;
    }
    //cerr << endl;
    k -= 1;
    return k;
}

int freeman_angle(const Polygon_i &poly, size_t i) {
    /* f:
     *        2
     *   3         1
     *        ^   
     *        |
     * 4 <--- X ---> 0
     *        |
     *        v
     *   5         7
     *        6
     *
     */
    size_t sz = poly.size();

    auto &p_last = poly[(i + sz - 1) % sz];
    auto &p_now = poly[i];
    auto dx = p_now[0] - p_last[0];
    auto dy = p_now[1] - p_last[1];
    /* both points must be neighbors */
    assert (-1 <= dx && dx <= 1);
    assert (-1 <= dy && dy <= 1);
    assert (!(dx == 0 && dy == 0));

    int lut[3][3] = {{3, 2, 1}, {4, -1, 0}, {5, 6, 7}};
    return lut[dy+1][dx+1];
}

double k_curvature(const Polygon_i &poly, size_t i, size_t k) {
    size_t sz = poly.size();
    double acc = 0;
    for (size_t idx = 0; idx < k; idx++) {
        acc += freeman_angle(poly, (i + 2*sz - idx) % sz) - freeman_angle(poly, (i+idx + 1) % sz);
    }
    return acc / k;
}

double k_cos(const Polygon_i &poly, size_t i, size_t k) {
    size_t sz = poly.size();
    int64_t x0 = poly[i][0], y0 = poly[i][1];
    int64_t x1 = poly[(i + sz + k) % sz][0], y1 = poly[(i + sz + k) % sz][1];
    int64_t x2 = poly[(i + sz - k) % sz][0], y2 = poly[(i + sz - k) % sz][1];
    auto xa = x0 - x1, ya = y0 - y1;
    auto xb = x0 - x2, yb = y0 - y2;
    auto dp = xa*yb + ya*xb;
    auto sq_a = xa*xa + ya*ya;
    auto sq_b = xb*xb + yb*yb;
    return dp / (sqrt(sq_a)*sqrt(sq_b));
}

ContourCallback gerbolyze::nopencv::simplify_contours_teh_chin(ContourCallback cb) {
    return [&cb](Polygon_i &poly, ContourPolarity cpol) {
        size_t sz = poly.size();
        vector<size_t> ros(sz);
        vector<double> sig(sz);
        vector<double> cur(sz);
        vector<bool> retain(sz);
        for (size_t i=0; i<sz; i++) {
            ros[i] = region_of_support(poly, i);
            sig[i] = fabs(k_cos(poly, i, ros[i]));
            cur[i] = k_curvature(poly, i, 1);
            retain[i] = true;
        }

        cerr << endl;
        cerr << "Polarity: " << cpol <<endl;
        cerr << "Coords:"<<endl;
        cerr << "  x: ";
        for (size_t i=0; i<sz; i++) {
            cerr << setfill(' ') << setw(2) << poly[i][0] << " ";
        }
        cerr << endl;
        cerr << "  y: ";
        for (size_t i=0; i<sz; i++) {
            cerr << setfill(' ') << setw(2) << poly[i][1] << " ";
        }
        cerr << endl;
        cerr << "Metrics:"<<endl;
        cerr << "ros: ";
        for (size_t i=0; i<sz; i++) {
            cerr << setfill(' ') << setw(2) << ros[i] << " ";
        }
        cerr << endl;
        cerr << "sig: ";
        for (size_t i=0; i<sz; i++) {
            cerr << setfill(' ') << setw(2) << sig[i] << " ";
        }
        cerr << endl;

        /* Pass 0 (like opencv): Remove points with zero 1-curvature */
        for (size_t i=0; i<sz; i++) {
            if (cur[i] == 0) {
                retain[i] = false;
                break;
            }
        }

        cerr << "pass 0: ";
        for (size_t i=0; i<sz; i++) {
            cerr << (retain[i] ? "#" : ".");
        }
        cerr << endl;

        /* 3a, Pass 1: Non-maxima suppression */
        for (size_t i=0; i<sz; i++) {
            for (size_t j=1; j<ros[i]/2; j++) {
                if (sig[i] < sig[(i + j) % sz] || sig[i] < sig[(i + sz - j) % sz]) {
                    retain[i] = false;
                    break;
                }
            }
        }

        cerr << "pass 1: ";
        for (size_t i=0; i<sz; i++) {
            cerr << (retain[i] ? "#" : ".");
        }
        cerr << endl;
        
        /* 3b, Pass 2: Zero-curvature suppression */
        for (size_t i=0; i<sz; i++) {
            if (retain[i] && ros[i] == 1) {
                if (sig[i] <= sig[(i + 1) % sz] || sig[i] <= sig[(i + sz - 1) % sz]) {
                    retain[i] = false;
                }
            }
        }

        cerr << "pass 2: ";
        for (size_t i=0; i<sz; i++) {
            cerr << (retain[i] ? "#" : ".");
        }
        cerr << endl;

        /* 3c, Pass 3: Further thinning */
        for (size_t i=0; i<sz; i++) {
            if (retain[i]) {
                if (ros[i] == 1) {
                    if (retain[(i + sz - 1) % sz] || retain[(i + 1)%sz]) {
                        if (sig[i] < sig[(i + sz - 1)%sz] || sig[i] < sig[(i + 1)%sz]) {
                            retain[i] = false;
                        }
                    }
                }
            }
        }

        cerr << "pass 3: ";
        for (size_t i=0; i<sz; i++) {
            cerr << (retain[i] ? "#" : ".");
        }
        cerr << endl;

        Polygon_i new_poly;
        for (size_t i=0; i<sz; i++) {
            if (retain[i]) {
                new_poly.push_back(poly[i]);
            }
        }
        cb(new_poly, cpol);
    };
}

