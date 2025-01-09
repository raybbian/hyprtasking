#pragma once

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprutils/math/Box.hpp>
#include <unordered_map>

#include "../types.hpp"

enum HTViewStage {
    HT_VIEW_ANIMATING,
    HT_VIEW_OPENED,
    HT_VIEW_CLOSED,
};

class HTLayoutBase {
  protected:
    // Same as monitor_id of the parent view
    VIEWID view_id;

  public:
    HTLayoutBase(VIEWID new_view_id);
    virtual ~HTLayoutBase() = default;

    virtual std::string layout_name() = 0;

    struct HTWorkspace {
        int x;
        int y;
        CBox box;
    };

    std::unordered_map<WORKSPACEID, HTWorkspace> overview_layout;

    virtual void on_show(std::function<void(void* thisptr)> on_complete = nullptr) = 0;
    virtual void on_hide(std::function<void(void* thisptr)> on_complete = nullptr) = 0;
    virtual void on_move(
        WORKSPACEID old_id,
        WORKSPACEID new_id,
        std::function<void(void* thisptr)> on_complete = nullptr
    ) = 0;

    // Should return true if when active, hyprtasking should manage the mouse button actions
    // (warping to appropriate position and smoothing the drag window, if it exists)
    virtual bool should_manage_mouse();
    // Called assuming that at least one overview is active (not nec on this monitor)
    virtual bool should_render_window(PHLWINDOW window);
    // The scale the drag window should be rendered at (about the mouse cursor)
    virtual float drag_window_scale();
    // Only to be called when closed, init/reset the position in case of config/monitor change
    virtual void init_position();
    // Populate overview_layout as if the overview was at a given stage
    virtual void build_overview_layout(HTViewStage stage);
    // Render the overview
    virtual void render();

    PHLMONITOR get_monitor();
    WORKSPACEID get_ws_id_from_global(Vector2D pos);
    WORKSPACEID get_ws_id_from_xy(int x, int y);
    std::pair<int, int> get_current_ws_xy();
    CBox get_global_window_box(PHLWINDOW window, WORKSPACEID workspace_id);
    CBox get_global_ws_box(WORKSPACEID workspace_id);

    Vector2D global_to_local_ws_scaled(Vector2D pos, WORKSPACEID workspace_id);
    Vector2D global_to_local_ws_unscaled(Vector2D pos, WORKSPACEID workspace_id);
    Vector2D local_ws_scaled_to_global(Vector2D pos, WORKSPACEID workspace_id);
    Vector2D local_ws_unscaled_to_global(Vector2D pos, WORKSPACEID workspace_id);
};
