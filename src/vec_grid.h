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
#include <vector>
#include <functional>
#include <gerbolyze.hpp>

namespace gerbolyze {

enum grid_type {
    POISSON_DISC,
    HEXGRID,
    SQUAREGRID
};

sampling_fun get_sampler(enum grid_type type);

std::vector<d2p> *sample_poisson_disc(double w, double h, double center_distance);
std::vector<d2p> *sample_hexgrid(double w, double h, double center_distance);
std::vector<d2p> *sample_squaregrid(double w, double h, double center_distance);

} /* namespace gerbolyze */

