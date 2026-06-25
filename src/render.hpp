#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprutils/math/Box.hpp>

void render_window_at_box(PHLWINDOW window, PHLMONITOR monitor, const Time::steady_tp& time, CBox box);

// Render a workspace's contents scaled into `box` (monitor-local, relative to (0,0)).
// Handles the active-workspace swap Hyprland requires to fully render a non-active
// workspace; the caller must restore monitor->m_activeWorkspace afterwards. `workspace`
// may be null, in which case only the layers/wallpaper are drawn.
void render_workspace_at_box(PHLMONITOR monitor, PHLWORKSPACE workspace, const Time::steady_tp& time, CBox box);
