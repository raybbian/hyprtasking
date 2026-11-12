#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <unordered_set>

#include "overview.hpp"

class HTManager {
  public:
    HTManager();

    std::vector<PHTVIEW> views;

    PHTVIEW get_view_from_monitor(PHLMONITOR pMonitor);
    PHTVIEW get_view_from_cursor();
    PHTVIEW get_view_from_id(VIEWID view_id);

    PHLWINDOW get_window_from_cursor(bool return_focused = true);

    void reset();

    void show_all_views();
    void hide_all_views();
    void show_cursor_view();

    void refresh_all_grid_caches();
    void remove_view_for_monitor_id(MONITORID mid);

    bool on_mouse_button(unsigned int button, bool pressed);
    void cancel_interaction();
    bool exit_to_workspace();
    bool on_key(IKeyboard::SKeyEvent event);
    bool on_mouse_move();
    bool on_mouse_axis(double delta);

    enum swipe_state_t {
        HT_SWIPE_OPEN,
        HT_SWIPE_MOVE,
        HT_SWIPE_NONE,
    };

    swipe_state_t swipe_state;
    float swipe_amt;
    std::unordered_set<uint32_t> jump_pressed_keys;
    void swipe_start();
    bool swipe_update(IPointer::SSwipeUpdateEvent e);
    bool swipe_end();

    bool has_active_view();
    bool cursor_view_active();

  private:
    enum class Interaction { Idle, PointerPressed, DraggingWindow, CompletingDrop, Cancelled };

    Interaction interaction = Interaction::Idle;
    unsigned int press_button = 0;
    Vector2D press_position;
    double press_threshold = 0;
    MONITORID press_monitor = MONITOR_INVALID;
    WORKSPACEID press_workspace = WORKSPACE_INVALID;
    PHLWINDOWREF press_window;
    PHLWORKSPACE previous_workspace;
    PHLWORKSPACE drag_source_workspace;
    CBox original_window_box;
    bool originally_floating = false;

    bool moved_past_threshold(Vector2D position) const;
    bool start_window_drag();
    bool end_window_drag();
};
