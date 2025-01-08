#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprutils/math/Vector2D.hpp>

namespace HTConfig {

std::string layout();

int bg_color();
int gap_size();
int border_size();
std::string exit_behavior();

int grid_rows();
int linear_height();

} // namespace HTConfig
