#pragma once

#include <cmath>

// Apply the same overview transform to texture and border corner radii.
// Keep Hyprland's negative outer-rounding sentinel unchanged.
inline int ht_scaled_radius(int radius, float scale) {
    return std::lround(radius * scale);
}

inline int ht_scaled_outer_radius(int radius, float scale) {
    return radius < 0 ? radius : ht_scaled_radius(radius, scale);
}
