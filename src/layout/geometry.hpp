#pragma once

#include <hyprutils/math/Box.hpp>

namespace HTGeometry {
    using Hyprutils::Math::CBox;
    using Hyprutils::Math::Vector2D;

    // Layout tiles are in output-oriented pixels. The cursor and monitor
    // origin are in global logical coordinates.
    inline CBox tile_to_global(const CBox& tile, Vector2D monitor_origin, double output_scale) {
        return {monitor_origin + tile.pos() / output_scale, tile.size() / output_scale};
    }

    inline Vector2D global_to_workspace(Vector2D point, const CBox& tile, Vector2D monitor_origin,
                                        double output_scale, double oriented_width) {
        const double tile_scale = tile.w / oriented_width;
        return ((point - monitor_origin) * output_scale - tile.pos()) / (output_scale * tile_scale);
    }

    inline Vector2D workspace_to_global(Vector2D point, const CBox& tile, Vector2D monitor_origin,
                                        double output_scale, double oriented_width) {
        const double tile_scale = tile.w / oriented_width;
        return monitor_origin + tile.pos() / output_scale + point * tile_scale;
    }
}
