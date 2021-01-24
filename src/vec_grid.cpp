/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2021 Jan Sebastian Götte <kicad@jaseg.de>
 * Copyright (C) 2021 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "poisson_disk_sampling.h"

#include "vec_grid.h"

using namespace std;
using namespace vectorizer;

sampling_fun vectorizer::get_sampler(enum grid_type type) {
    switch(type) {
        case POISSON_DISC:
            return sample_poisson_disc;
        case HEXGRID:
            return sample_hexgrid;
        case SQUAREGRID:
            return sample_squaregrid;
        default:
            return sample_poisson_disc;
    }
}

vector<d2p> *vectorizer::sample_poisson_disc(double w, double h, double center_distance) {
    d2p top_left {0, 0};
    d2p bottom_right {w, h};
    return new auto(thinks::PoissonDiskSampling(center_distance, top_left, bottom_right));
}

vector<d2p> *vectorizer::sample_hexgrid(double w, double h, double center_distance) {
    double radius = center_distance / 2.0 / (sqrt(3) / 2.0); /* radius of hexagon */
    double pitch_v = 1.5 * radius;
    double pitch_h = center_distance;

    /* offset of first hexagon to make sure the entire area is covered. We use slightly larger values here to avoid
     * corner cases during clipping in the voronoi map generator.  The inaccuracies this causes at the edges are
     * negligible. */
    double off_x = 0.5001 * center_distance;
    double off_y = 0.5001 * radius;

    /* NOTE: The voronoi generator is not quite stable when points lie outside the bounds. Thus, floor(). */
    long long int points_x = floor(w / pitch_h);
    long long int points_y = floor(h / pitch_v);

    vector<d2p> *out = new vector<d2p>();
    out->reserve((points_x+1) * points_y);

    /* This may generate up to one extra row of points. We don't care since these points will simply be clipped during
     * voronoi map generation. */
    for (long long int y_i=0; y_i<points_y; y_i+=2) {
        for (long long int x_i=0; x_i<points_x; x_i++) { /* allow one extra point to compensate for row shift */
            out->push_back(d2p{off_x + x_i * pitch_h, off_y + y_i * pitch_v});
        }

        for (long long int x_i=0; x_i<points_x+1; x_i++) { /* allow one extra point to compensate for row shift */
            out->push_back(d2p{off_x + (x_i - 0.5) * pitch_h, off_y + (y_i + 1) * pitch_v});
        }
    }

    return out;
}

vector<d2p> *vectorizer::sample_squaregrid(double w, double h, double center_distance) {
    /* offset of first square to make sure the entire area is covered. We use slightly larger values here to avoid
     * corner cases during clipping in the voronoi map generator.  The inaccuracies this causes at the edges are
     * negligible. */
    double off_x = 0.5 * center_distance;
    double off_y = 0.5 * center_distance;

    long long int points_x = ceil(w / center_distance);
    long long int points_y = ceil(h / center_distance);

    vector<d2p> *out = new vector<d2p>();
    out->reserve(points_x * points_y);

    for (long long int y_i=0; y_i<points_y; y_i++) {
        for (long long int x_i=0; x_i<points_x; x_i++) {
            out->push_back({off_x + x_i*center_distance, off_y + y_i*center_distance});
        }
    }

    return out;
}

