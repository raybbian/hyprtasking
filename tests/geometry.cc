#include "../src/layout/geometry.hpp"

#include <cmath>
#include <iostream>

using namespace HTGeometry;

int main() {
    const CBox portrait_tile{20, 40, 250, 400};
    const Vector2D origin{-800, -120};
    constexpr double output_scale = 1.25;
    constexpr double oriented_width = 800;

    const auto global_box = tile_to_global(portrait_tile, origin, output_scale);
    if (global_box.pos() != Vector2D{-784, -88} || global_box.size() != Vector2D{200, 320}) {
        std::cerr << "Incorrect tile bounds on a scaled portrait output\n";
        return 1;
    }

    const Vector2D window_point{523, 978};
    const auto pointer = workspace_to_global(window_point, portrait_tile, origin,
                                             output_scale, oriented_width);
    const auto result = global_to_workspace(pointer, portrait_tile, origin,
                                            output_scale, oriented_width);
    if (!global_box.containsPoint(pointer) || std::abs(result.x - window_point.x) > 1e-8
        || std::abs(result.y - window_point.y) > 1e-8) {
        std::cerr << "The workspace coordinate did not round trip through the preview\n";
        return 1;
    }
}
