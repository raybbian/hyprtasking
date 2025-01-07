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
    VIEWID view_id;

  public:
    HTLayoutBase(VIEWID new_view_id);
    virtual ~HTLayoutBase() = default;

    struct HTWorkspace {
        int x;
        int y;
        CBox box;
    };

    std::unordered_map<WORKSPACEID, HTWorkspace> overview_layout;

    virtual void on_show(std::function<void(void* thisptr)> on_complete = nullptr);
    virtual void on_hide(std::function<void(void* thisptr)> on_complete = nullptr);
    virtual void
    on_move(WORKSPACEID ws_id, std::function<void(void* thisptr)> on_complete = nullptr);

    virtual float drag_window_scale();
    virtual void init_position();
    virtual void build_overview_layout(HTViewStage stage);
    virtual void render();

    PHLMONITOR get_monitor();
    WORKSPACEID get_ws_id_from_global(Vector2D pos);
    CBox get_global_window_box(PHLWINDOW window, WORKSPACEID workspace_id);

    Vector2D global_to_local_ws_scaled(Vector2D pos, WORKSPACEID workspace_id);
    Vector2D global_to_local_ws_unscaled(Vector2D pos, WORKSPACEID workspace_id);
    Vector2D local_ws_scaled_to_global(Vector2D pos, WORKSPACEID workspace_id);
    Vector2D local_ws_unscaled_to_global(Vector2D pos, WORKSPACEID workspace_id);
};
