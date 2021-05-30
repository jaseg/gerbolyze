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

        typedef std::function<void(Polygon, ContourPolarity)> ContourCallback;

        class Image32 {
        public:
            Image32(int size_x, int size_y, const int32_t *data=nullptr) {
                assert(size_x > 0 && size_x < 100000);
                assert(size_y > 0 && size_y < 100000);
                m_data = new int32_t[size_x * size_y] { 0 };
                m_rows = size_y;
                m_cols = size_x;
                if (data != nullptr) {
                    memcpy(m_data, data, sizeof(int32_t) * size_x * size_y);
                }
            }

            Image32(const Image32 &other) : Image32(other.cols(), other.rows(), other.ptr()) {}

            ~Image32() {
                delete m_data;
            }
            
            int32_t &at(int x, int y) {
                assert(x >= 0 && y >= 0 && x < m_cols && y < m_rows);
                assert(m_data != nullptr);

                return m_data[y*m_cols + x];
            };

            void set_at(int x, int y, int val) {
                assert(x >= 0 && y >= 0 && x < m_cols && y < m_rows);
                assert(m_data != nullptr);

                m_data[y*m_cols + x] = val;
                cerr << "set_at " << x << " " << y << ": " << val << " -> " << at(x, y) << endl;
            };

            const int32_t &at(int x, int y) const {
                assert(x >= 0 && y >= 0 && x < m_cols && y < m_rows);
                assert(m_data != nullptr);

                return m_data[y*m_cols + x];
            };

            int32_t at_default(int x, int y, int32_t default_value=0) const {
                assert(m_data != nullptr);

                if (x >= 0 && y >= 0 && x < m_cols && y < m_rows) {
                    return at(x, y);

                } else {
                    return default_value;
                }
            };

            int rows() const { return m_rows; }
            int cols() const { return m_cols; }
            const int32_t *ptr() const { return m_data; }

        private:
            int32_t *m_data = nullptr;
            int m_rows=0, m_cols=0;
        };

        void find_blobs(Image32 &img, ContourCallback cb);
    }
}

