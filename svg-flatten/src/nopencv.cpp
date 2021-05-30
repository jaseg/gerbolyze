
#include <iostream>

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
    D_S,
    D_SW,
    D_W,
    D_NW,
    D_N,
    D_NE,
    D_E,
    D_SE
};

static void follow(gerbolyze::nopencv::Image32 img, int start_x, int start_y, Direction initial_direction, int nbd, int connectivity, Polygon &poly) {

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
        poly.emplace_back(d2p{(double)start_x,   (double)start_y});
        poly.emplace_back(d2p{(double)start_x+1, (double)start_y});
        poly.emplace_back(d2p{(double)start_x+1, (double)start_y+1});
        poly.emplace_back(d2p{(double)start_x,   (double)start_y+1});
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
                case 0: poly.emplace_back(d2p{(double)center_x,   (double)center_y}); break;
                case 2: poly.emplace_back(d2p{(double)center_x+1, (double)center_y}); break;
                case 4: poly.emplace_back(d2p{(double)center_x+1, (double)center_y+1}); break;
                case 6: poly.emplace_back(d2p{(double)center_x,   (double)center_y+1}); break;
            }
        }

        center_x = probe_x;
        center_y = probe_y;
        current_direction = flip_direction[k % 8];

        //cerr << "  " << center_x << " " << center_y << " / " << dir_str[current_direction] << " -> " << set_val << endl;
    } while (center_x != start_x || center_y != start_y || current_direction != start_direction);
}


void gerbolyze::nopencv::find_blobs(gerbolyze::nopencv::Image32 img, gerbolyze::nopencv::ContourCallback cb) {
    int nbd = 1;
    Polygon poly;
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
                follow(img, x, y, D_E, nbd, 4, poly);
                cb(poly, CP_HOLE);
                poly.clear();
            }
        }
    }
}

