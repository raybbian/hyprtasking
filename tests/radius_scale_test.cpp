#include "../src/radius_scale.hpp"

#include <cassert>

int main() {
    // Texture, border and optional outer-border corners all share a scale.
    assert(ht_scaled_radius(12, 1.f) == 12);
    assert(ht_scaled_radius(12, 0.5f) == 6);
    assert(ht_scaled_radius(12, 1.5f) == 18);
    assert(ht_scaled_radius(12, 2.f) == 24);
    assert(ht_scaled_radius(0, 0.5f) == 0);

    // Fractional scales round once, consistently, at the final radius.
    assert(ht_scaled_radius(7, 0.5f) == 4);
    assert(ht_scaled_radius(9, 0.5f) == 5);
    assert(ht_scaled_radius(13, 0.75f) == 10);
    assert(ht_scaled_outer_radius(7, 0.5f) == 4);
    assert(ht_scaled_outer_radius(0, 1.5f) == 0);

    // Hyprland uses -1 to request default rounding, not a numeric radius.
    assert(ht_scaled_outer_radius(-1, 0.5f) == -1);
    assert(ht_scaled_outer_radius(-1, 2.f) == -1);
    return 0;
}
