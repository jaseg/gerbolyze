
#include <iostream>
#include <iomanip>
#include <stack>

#include "nopencv.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize.h>

#define IIR_GAUSS_BLUR_IMPLEMENTATION
#include "iir_gauss_blur.h"

template void iir_gauss_blur<uint8_t>(unsigned int width, unsigned int height, unsigned char components, uint8_t* image, float sigma);
template void iir_gauss_blur<uint32_t> (unsigned int width, unsigned int height, unsigned char components, uint32_t* image, float sigma);
template void iir_gauss_blur<float> (unsigned int width, unsigned int height, unsigned char components, float* image, float sigma);

using namespace gerbolyze;
using namespace gerbolyze::nopencv;

static constexpr bool debug = false;

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

const char * const dir_str[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };

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

    if (debug) {
        cerr << "follow " << start_x << " " << start_y << " | dir=" << dir_str[initial_direction] << " nbd=" << nbd << " conn=" << connectivity << endl;
    }

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
        /* We must return these vertices counter-clockwise! */
        poly.emplace_back(i2p{start_x,   start_y+1});
        poly.emplace_back(i2p{start_x+1, start_y+1});
        poly.emplace_back(i2p{start_x+1, start_y});
        poly.emplace_back(i2p{start_x,   start_y});

        return;
    }

    /* starting point found. */
    int current_direction = k % 8;
    int start_direction = current_direction;
    int center_x = start_x, center_y = start_y;

    if (debug) {
        cerr << "  init: " << center_x << " " << center_y << " / " << dir_str[current_direction] << endl;
    }

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

        if (debug) {
            cerr << "  " << center_x << " " << center_y << " / " << dir_str[current_direction] << " -> " << set_val << endl;
        }
    } while (center_x != start_x || center_y != start_y || current_direction != start_direction);
}


void gerbolyze::nopencv::find_contours(gerbolyze::nopencv::Image32 &img, gerbolyze::nopencv::ContourCallback cb) {
    /* Implementation of the hierarchical contour finding algorithm from Suzuki and Abe, 1983: Topological Structural
     * Analysis of Digitized Binary Images by Border Following
     *
     * Written with these two resources as reference:
     *     https://theailearner.com/tag/suzuki-contour-algorithm-opencv/
     *     https://github.com/FreshJesh5/Suzuki-Algorithm/blob/master/contoursv1/contoursv1.cpp
     *
     * WARNING: input image MUST BE BINARIZE: All pixels must have value either 0 or 1. Otherwise, chaos ensues.
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

        if (debug) {
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
        }

        /* Pass 0 (like opencv): Remove points with zero 1-curvature */
        for (size_t i=0; i<sz; i++) {
            if (cur[i] == 0) {
                retain[i] = false;
                break;
            }
        }

        if (debug) {
            cerr << "pass 0: ";
            for (size_t i=0; i<sz; i++) {
                cerr << (retain[i] ? "#" : ".");
            }
            cerr << endl;
        }

        /* 3a, Pass 1: Non-maxima suppression */
        for (size_t i=0; i<sz; i++) {
            for (size_t j=1; j<ros[i]/2; j++) {
                if (sig[i] < sig[(i + j) % sz] || sig[i] < sig[(i + sz - j) % sz]) {
                    retain[i] = false;
                    break;
                }
            }
        }

        if (debug) {
            cerr << "pass 1: ";
            for (size_t i=0; i<sz; i++) {
                cerr << (retain[i] ? "#" : ".");
            }
            cerr << endl;
        }
        
        /* 3b, Pass 2: Zero-curvature suppression */
        for (size_t i=0; i<sz; i++) {
            if (retain[i] && ros[i] == 1) {
                if (sig[i] <= sig[(i + 1) % sz] || sig[i] <= sig[(i + sz - 1) % sz]) {
                    retain[i] = false;
                }
            }
        }

        if (debug) {
            cerr << "pass 2: ";
            for (size_t i=0; i<sz; i++) {
                cerr << (retain[i] ? "#" : ".");
            }
            cerr << endl;
        }

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

        if (debug) {
            cerr << "pass 3: ";
            for (size_t i=0; i<sz; i++) {
                cerr << (retain[i] ? "#" : ".");
            }
            cerr << endl;
        }

        Polygon_i new_poly;
        for (size_t i=0; i<sz; i++) {
            if (retain[i]) {
                new_poly.push_back(poly[i]);
            }
        }
        
        if (!new_poly.empty()) {
            cb(new_poly, cpol);
        }
    };
}

static double dp_eps(double dx, double dy) {
    /* Implementation of:
     *
     * Prasad, Dilip K., et al. "A novel framework for making dominant point detection methods non-parametric."
     * Image and Vision Computing 30.11 (2012): 843-859.
     * https://core.ac.uk/download/pdf/131287229.pdf
     *
     * For another implementation, see:
     * https://github.com/BobLd/RamerDouglasPeuckerNetV2/blob/master/RamerDouglasPeuckerNetV2.Test/RamerDouglasPeuckerNetV2/RamerDouglasPeucker.cs
     */
    double m = dy / dx;
    double s = sqrt(pow(dx, 2) + pow(dy, 2));
    double phi = atan(m);
    double t_max = 1/s * (fabs(cos(phi)) + fabs(sin(phi)));
    double t_max_polynomial = 1 - t_max + pow(t_max, 2);
    return s * fmax(
            atan(1/s * fabs(sin(phi) + cos(phi)) * t_max_polynomial),
            atan(1/s * fabs(sin(phi) - cos(phi)) * t_max_polynomial));
}

/* a, b inclusive */
static array<size_t, 3> dp_step(Polygon_i &poly, size_t a, size_t b) {

    double dx = poly[b][0] - poly[a][0];
    double dy = poly[b][1] - poly[a][1];
    double eps = dp_eps(dx, dy);

    size_t max_idx = 0;
    double max_dist = 0;
    /* https://en.wikipedia.org/wiki/Distance_from_a_point_to_a_line */
    double dist_ab = sqrt(pow(poly[b][0] - poly[a][0], 2) + pow(poly[b][1] - poly[a][1], 2));
    for (size_t i=a+1; i<b; i++) {
        double dist_i = fabs(
                  (poly[b][0] - poly[a][0]) * (poly[a][1] - poly[i][1])
                - (poly[a][0] - poly[i][0]) * (poly[b][1] - poly[a][1]))
            / dist_ab;
        if (dist_i > max_dist && dist_i > eps) {
            max_dist = dist_i;
            max_idx = i;
        }
    }

    return {a, max_idx, b};
}

ContourCallback gerbolyze::nopencv::simplify_contours_douglas_peucker(ContourCallback cb) {
    return [&cb](Polygon_i &poly, ContourPolarity cpol) {

        Polygon_i out;
        out.push_back(poly[0]);

        stack<array<size_t, 3>> indices;
        indices.push(dp_step(poly, 0, poly.size()-1));

        while (!indices.empty()) {
            auto idx = indices.top();
            indices.pop(); /* awesome C++ api let's goooooo */

            if (idx[1] > 0) {
                indices.push(dp_step(poly, idx[0], idx[1]));

                indices.push(dp_step(poly, idx[1], idx[2]));

            } else {
                out.push_back(poly[idx[2]]);
            }
        }


        cb(out, cpol);
    };
}

double gerbolyze::nopencv::polygon_area(Polygon_i &poly) {
    double acc = 0;
    size_t prev = poly.size() - 1;
    for (size_t cur=0; cur<poly.size(); cur++) {
        acc += (poly[prev][0] + poly[cur][0]) * (poly[prev][1] - poly[cur][1]);
        prev = cur;
    }
    return acc / 2;
}

double gerbolyze::nopencv::polygon_perimeter(Polygon_i &poly) {
    double acc = 0;
    size_t prev = poly.size() - 1;
    for (size_t cur=0; cur<poly.size(); cur++) {
        double dx = poly[cur][0] - poly[prev][0];
        double dy = poly[cur][1] - poly[prev][1];
        acc += sqrt(dx*dx + dy*dy);
        prev = cur;
    }
    return acc;
}

d2p gerbolyze::nopencv::polygon_centroid(Polygon_i &poly) {
    double acc_x = 0, acc_y = 0;

    double area = polygon_area(poly);
    size_t prev = poly.size() - 1;
    for (size_t cur=0; cur<poly.size(); cur++) {
        double a = poly[prev][1]*poly[cur][0] - poly[cur][1]*poly[prev][0];
        acc_x += (poly[prev][0] + poly[cur][0]) * a;
        acc_y += (poly[prev][1] + poly[cur][1]) * a;
        prev = cur;
    }

    return { acc_x / (6*area), acc_y / (6*area) };
}

template<typename T>
gerbolyze::nopencv::Image<T>::Image(int size_x, int size_y, const T *data) {
    assert(size_x > 0 && size_x < 100000);
    assert(size_y > 0 && size_y < 100000);
    m_data = new T[size_x * size_y] { 0 };
    m_rows = size_y;
    m_cols = size_x;
    if (data != nullptr) {
        memcpy(m_data, data, sizeof(T) * size_x * size_y);
    }
}

template<typename T>
bool gerbolyze::nopencv::Image<T>::load(const char *filename) {
    return stb_to_internal(stbi_load(filename, &m_cols, &m_rows, nullptr, 1));
}

template<typename T>
bool gerbolyze::nopencv::Image<T>::load_memory(const void *buf, size_t len) {
    return stb_to_internal(stbi_load_from_memory(reinterpret_cast<const uint8_t *>(buf), len, &m_cols, &m_rows, nullptr, 1));
}

template<typename T>
void gerbolyze::nopencv::Image<T>::binarize(T threshold) {
    assert(m_data != nullptr);
    assert(m_rows > 0 && m_cols > 0);

    for (int y=0; y<m_rows; y++) {
        for (int x=0; x<m_cols; x++) {
            m_data[y*m_cols + x] = m_data[y*m_cols + x] >= threshold;
        }
    }
}

template<typename T>
bool gerbolyze::nopencv::Image<T>::stb_to_internal(uint8_t *data) {
    if (data == nullptr)
        return false;

    if (m_rows < 0 || m_rows > 100000)
        return false;
    if (m_cols < 0 || m_cols > 100000)
        return false;

    m_data = new T[size()] { 0 };
    for (int y=0; y<m_rows; y++) {
        for (int x=0; x<m_cols; x++) {
            m_data[y*m_cols + x] = data[y*m_cols + x];
        }
    }

    stbi_image_free(data);
    return true;
}

template<typename T>
void gerbolyze::nopencv::Image<T>::blur(int radius) {
    iir_gauss_blur(m_cols, m_rows, 1, m_data, radius/2.0);
}

template<>
void gerbolyze::nopencv::Image<float>::resize(int new_w, int new_h) {
    float *old_data = m_data;
    m_data = new float[new_w * new_h];
    stbir_resize_float(old_data, m_cols, m_rows, 0,
                        m_data, new_w, new_h, 0,
                        1);
    m_cols = new_w;
    m_rows = new_h;
}

template<>
void gerbolyze::nopencv::Image<uint8_t>::resize(int new_w, int new_h) {
    uint8_t *old_data = m_data;
    m_data = new uint8_t[new_w * new_h];
    stbir_resize_uint8(old_data, m_cols, m_rows, 0,
                        m_data, new_w, new_h, 0,
                        1);
    m_cols = new_w;
    m_rows = new_h;
}

template gerbolyze::nopencv::Image<int32_t>::Image(int size_x, int size_y, const int32_t *data);
template bool gerbolyze::nopencv::Image<int32_t>::load(const char *filename);
template bool gerbolyze::nopencv::Image<int32_t>::load_memory(const void *buf, size_t len);
template void gerbolyze::nopencv::Image<int32_t>::binarize(int32_t threshold);
template bool gerbolyze::nopencv::Image<int32_t>::stb_to_internal(uint8_t *data);
template void gerbolyze::nopencv::Image<int32_t>::blur(int radius);

template gerbolyze::nopencv::Image<uint8_t>::Image(int size_x, int size_y, const uint8_t *data);
template bool gerbolyze::nopencv::Image<uint8_t>::load(const char *filename);
template bool gerbolyze::nopencv::Image<uint8_t>::load_memory(const void *buf, size_t len);
template void gerbolyze::nopencv::Image<uint8_t>::binarize(uint8_t threshold);
template bool gerbolyze::nopencv::Image<uint8_t>::stb_to_internal(uint8_t *data);
template void gerbolyze::nopencv::Image<uint8_t>::blur(int radius);

template gerbolyze::nopencv::Image<float>::Image(int size_x, int size_y, const float *data);
template bool gerbolyze::nopencv::Image<float>::load(const char *filename);
template bool gerbolyze::nopencv::Image<float>::load_memory(const void *buf, size_t len);
template void gerbolyze::nopencv::Image<float>::binarize(float threshold);
template bool gerbolyze::nopencv::Image<float>::stb_to_internal(uint8_t *data);
template void gerbolyze::nopencv::Image<float>::blur(int radius);
