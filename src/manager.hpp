#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>

#include "overview.hpp"

class HTManager {
  public:
    HTManager();

    std::vector<PHTVIEW> views;

    PHTVIEW get_view_from_monitor(PHLMONITOR pMonitor);
    PHTVIEW get_view_from_cursor();
    PHTVIEW get_view_from_id(VIEWID view_id);

    PHLWINDOW get_window_from_cursor();

    void reset();

    void show_all_views();
    void hide_all_views();
    void show_cursor_view();

    bool start_window_drag();
    bool end_window_drag();
    bool exit_to_workspace();
    bool on_mouse_move();
    bool on_mouse_axis(double delta);

    bool has_active_view();
    bool cursor_view_active();
};
