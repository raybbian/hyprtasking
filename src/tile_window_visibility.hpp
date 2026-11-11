#pragma once

inline constexpr bool
hyprtasking_tile_accepts_window(bool rendering_tile, bool pinned, bool special, bool matches_tile) {
    return !rendering_tile || pinned || special || matches_tile;
}
