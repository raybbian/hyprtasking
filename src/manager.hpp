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

    bool workspace_drag_button(unsigned int button, bool pressed);
    bool workspace_drag_motion();
    bool cancel_workspace_drag();
    void render_workspace_handles(Render::CRenderContext& ctx, const PHTVIEW& view);

    bool start_window_drag();
    bool end_window_drag();
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
    struct WorkspacePress {
        MONITORID monitor;
        WORKSPACEID workspace;
        Vector2D origin;
        bool dragging = false;
    };
    std::optional<WorkspacePress> workspace_press;

    CBox workspace_handle(const CBox& tile, double scale) const;
    bool can_swap_tiles(const PHTVIEW& source, WORKSPACEID source_id,
                        const PHTVIEW& destination, WORKSPACEID destination_id) const;
    bool swap_workspace_contents(const WorkspacePress& source);
};
