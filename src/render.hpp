#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprutils/math/Box.hpp>

void render_window_at_box(PHLWINDOW window, PHLMONITOR monitor, timespec* time, CBox box);
