#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>

#include "overview.hpp"

struct HTManager {
  public:
    HTManager();

    std::vector<PHTVIEW> views;

    PHTVIEW get_view_from_monitor(PHLMONITOR pMonitor);
    PHTVIEW get_view_from_cursor();

    void reset();

    void show_all_views();
    void hide_all_views();
    void show_cursor_view();

    void start_window_drag();
    void end_window_drag();
    void exit_to_workspace();
    // void on_mouse_button(bool pressed, uint32_t button);
    void on_mouse_move();

    bool has_active_view();
    bool cursor_view_active();

    bool should_render_window(PHLWINDOW pWindow, PHLMONITOR pMonitor);
};
