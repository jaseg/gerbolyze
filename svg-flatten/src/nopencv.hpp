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

#include <array>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <assert.h>

#include "geom2d.hpp"

using namespace std;

namespace gerbolyze {
    namespace nopencv {

        enum ContourPolarity {
            CP_CONTOUR,
            CP_HOLE
        };

        typedef std::function<void(Polygon_i&, ContourPolarity)> ContourCallback;

        template<typename T> class Image {
        public:
            Image() {}
            Image(int w, int h, const T *data=nullptr);
            Image(const Image<T> &other) : Image<T>(other.cols(), other.rows(), other.ptr()) {}
            template<typename U> Image(const Image<U> &other) : Image<T>(other.cols(), other.rows()) {
                for (size_t y=0; y<m_rows; y++) {
                    for (size_t x=0; x<m_cols; x++) {
                        at(x, y) = other.at(x, y);
                    }
                }
            }

            ~Image() {
                if (m_data) {
                    delete m_data;
                }
            }
            
            bool load(const char *filename);
            bool load_memory(const void *buf, size_t len);
            void binarize();

            T &at(int x, int y) {
                assert(x >= 0 && y >= 0 && x < m_cols && y < m_rows);
                assert(m_data != nullptr);

                return m_data[y*m_cols + x];
            };

            void set_at(int x, int y, T val) {
                assert(x >= 0 && y >= 0 && x < m_cols && y < m_rows);
                assert(m_data != nullptr);

                m_data[y*m_cols + x] = val;
                cerr << "set_at " << x << " " << y << ": " << val << " -> " << at(x, y) << endl;
            };

            const T &at(int x, int y) const {
                assert(x >= 0 && y >= 0 && x < m_cols && y < m_rows);
                assert(m_data != nullptr);

                return m_data[y*m_cols + x];
            };

            T at_default(int x, int y, T default_value=0) const {
                assert(m_data != nullptr);

                if (x >= 0 && y >= 0 && x < m_cols && y < m_rows) {
                    return at(x, y);

                } else {
                    return default_value;
                }
            };

            void blur(int radius);
            void resize(int new_w, int new_h);

            int rows() const { return m_rows; }
            int cols() const { return m_cols; }
            int size() const { return m_cols*m_rows; }
            const T *ptr() const { return m_data; }

        private:
            bool stb_to_internal(uint8_t *data);

            T *m_data = nullptr;
            int m_rows=0, m_cols=0;
        };

        typedef Image<uint8_t> Image8;
        typedef Image<int32_t> Image32;
        typedef Image<float> Image32f;

        void find_contours(Image32 &img, ContourCallback cb);
        ContourCallback simplify_contours_teh_chin(ContourCallback cb);

        double polygon_area(Polygon_i &poly);
    }
}

