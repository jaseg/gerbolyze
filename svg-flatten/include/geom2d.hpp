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
#include <iomanip>
#include <cmath>
#include <algorithm>

#include <clipper.hpp>

#include "svg_import_defs.h"

using namespace std;

namespace gerbolyze {

    typedef std::array<double, 2> d2p;
    typedef std::vector<d2p> Polygon;

    typedef std::array<int64_t, 2> i2p;
    typedef std::vector<i2p> Polygon_i;

    class xform2d {
        public:
            xform2d(double xx, double yx, double xy, double yy, double x0=0.0, double y0=0.0) :
                xx(xx), yx(yx), xy(xy), yy(yy), x0(x0), y0(y0) {}
            
            xform2d() : xform2d(1.0, 0.0, 0.0, 1.0) {}

            xform2d(const string &svg_transform) : xform2d() {
                if (svg_transform.empty())
                    return;

                string start("matrix(");
                if (svg_transform.substr(0, start.length()) != start)
                    return;
                if (svg_transform.back() != ')')
                    return;

                const string &foo = svg_transform.substr(start.length(), svg_transform.length());
                const string &bar = foo.substr(0, foo.length() - 1);

                istringstream xform(bar);

                double a, c, e,
                       b, d, f;
                xform >> a >> b >> c >> d >> e >> f;
                if (xform.fail())
                    return;

                xx=a, yx=b, xy=c, yy=d, x0=e, y0=f;
                //cerr << "xform loaded " << dbg_str() << endl;
            }

            xform2d &translate(double x, double y) {
                xform2d xf(1, 0, 0, 1, x, y);
                transform(xf);
                return *this;
            }

            xform2d &scale(double x, double y) {
                xform2d xf(x, 0, 0, y);
                transform(xf);
                return *this;
            }

            xform2d &transform(const xform2d &other) {
                double n_xx = other.xx * xx + other.yx * xy;
                double n_yx = other.xx * yx + other.yx * yy;

                double n_xy = other.xy * xx + other.yy * xy;
                double n_yy = other.xy * yx + other.yy * yy;

                double n_x0 = other.x0 * xx + other.y0 * xy + x0;
                double n_y0 = other.x0 * yx + other.y0 * yy + y0;

                xx = n_xx;
                yx = n_yx;
                xy = n_xy;
                yy = n_yy;
                x0 = n_x0;
                y0 = n_y0;

                return *this;
            };

            double doc2phys_dist(double dist_doc) {
                return dist_doc * sqrt(xx*xx + xy*xy);
            }

            double phys2doc_dist(double dist_doc) {
                return dist_doc / sqrt(xx*xx + xy*xy);
            }

            double doc2phys_skew(double dist_doc) {
                /* https://math.stackexchange.com/a/3521141 */
                /* https://stackoverflow.com/a/70381885 */
                /* xx yx x0
                 * xy yy y0 */
                double s_x = sqrt(xx*xx + xy*xy);

                if (xx == 0 && xy == 0) {
                    return std::numeric_limits<double>::infinity;
                }

                double theta = atan2(xy, xx);
                double f = (xx*yy - xy*yx);

                if (f == 0) {
                    return std::numeric_limits<double>::infinity;
                }

                double m = (xx*yx + yy*xy) / f;

                double f = xx + m*xy;
                double s_y = 0;

                if (f == 0) {
                    f = m*xx - xy;
                    if (f == 0) {
                        return std::numeric_limits<double>::infinity;
                    }
                    s_y = yx*s_x / f;
                } else {
                    s_y = yy*s_x / f;
                }

                return s_x - s_y > 
            }

            double doc2phys_min(double dist_doc) {
                return dist_doc * fmin(sqrt(xx*xx + xy*xy), sqrt(yy*yy + yx*yx));
            }

            double doc2phys_max(double dist_doc) {
                return dist_doc * fmax(sqrt(xx*xx + xy*xy), sqrt(yy*yy + yx*yx));
            }

            d2p doc2phys(const d2p p) {
                return d2p {
                    xx * p[0] + xy * p[1] + x0,
                    yx * p[0] + yy * p[1] + y0
                };
            }

            xform2d &invert(bool *success_out=nullptr) {
                /* From Cairo source */

                /* inv (A) = 1/det (A) * adj (A) */
                double det = xx*yy - yx*xy;

                if (det == 0 || !isfinite(det)) {
                    if (success_out)
                        *success_out = false;
                    *this = xform2d(); /* unity matrix */
                    return *this;
                }

                *this = xform2d(yy/det, -yx/det,
                        -xy/det, xx/det,
                        (xy*y0 - yy*x0)/det, (yx*x0 - xx*y0)/det);

                if (success_out)
                    *success_out = true;
                return *this;
            }

            /* Transform given clipper paths */
            void doc2phys_clipper(ClipperLib::Paths &paths) {
                for (auto &p : paths) {
                    doc2phys_clipper(p);
                }
            }

            void doc2phys_clipper(ClipperLib::Path &path) {
                std::transform(path.begin(), path.end(), path.begin(),
                        [this](ClipperLib::IntPoint p) -> ClipperLib::IntPoint {
                            d2p out(this->doc2phys(d2p{p.X / clipper_scale, p.Y / clipper_scale}));
                            return {
                                (ClipperLib::cInt)round(out[0] * clipper_scale),
                                (ClipperLib::cInt)round(out[1] * clipper_scale)
                            };
                        });
            }

            /* Transform given clipper paths */
            void phys2doc_clipper(ClipperLib::Paths &paths) {
                for (auto &p : paths) {
                    phys2doc_clipper(p);
                }
            }

            void phys2doc_clipper(ClipperLib::Path &path) {
                std::transform(path.begin(), path.end(), path.begin(),
                        [this](ClipperLib::IntPoint p) -> ClipperLib::IntPoint {
                            d2p out(this->doc2phys(d2p{p.X / clipper_scale, p.Y / clipper_scale}));
                            return {
                                (ClipperLib::cInt)round(out[0] * clipper_scale),
                                (ClipperLib::cInt)round(out[1] * clipper_scale)
                            };
                        });
            }

            void transform_polygon(Polygon &poly) {
                std::transform(poly.begin(), poly.end(), poly.begin(),
                        [this](d2p p) -> d2p {
                            return this->doc2phys(d2p{p[0], p[1]});
                        });
            }

            string dbg_str() {
                ostringstream os;
                os << "xform2d< " << setw(5);
                os << xx << ", " << xy << ", " << x0 << " / ";
                os << yy << ", " << yx << ", " << y0;
                os << " >";
                return os.str();
            }

        private:
            double xx, yx,
                   xy, yy,
                   x0, y0;
    };
}
