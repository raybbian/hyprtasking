#pragma once

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprutils/math/Box.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "../types.hpp"

class HTView;
typedef SP<HTView> PHTVIEW;

enum HTViewStage {
    HT_VIEW_ANIMATING,
    HT_VIEW_OPENED,
    HT_VIEW_CLOSED,
};

class HTLayoutBase {
  protected:
    VIEWID view_id;
    PHTVIEW par_view;

    float cached_border_size = 0.f;

    void update_render_cache();
    void render_workspace(PHLWORKSPACE ws, CBox render_box, bool is_active);
    void render_dragged_window();
    void render_border(CBox box, bool is_active);

  public:
    using CallbackFun = Hyprutils::Animation::CBaseAnimatedVariable::CallbackFun;

    HTLayoutBase(VIEWID new_view_id);
    virtual ~HTLayoutBase() = default;

    virtual std::string layout_name() = 0;

    int layer = 0;
    struct HTWorkspace {
        int x;
        int y;
        CBox box;
        WORKSPACEID id = WORKSPACE_INVALID;
        std::string name;
        MONITORID monitor_id = -1;
    };

    virtual CBox calculate_ws_box(int x, int y, HTViewStage stage) = 0;
    std::unordered_map<WORKSPACEID, HTWorkspace> overview_layout;

    virtual void close_open_lerp(float perc) = 0;
    virtual void on_show(CallbackFun on_complete = nullptr) = 0;
    virtual void on_hide(CallbackFun on_complete = nullptr) = 0;
    virtual void
    on_move(WORKSPACEID old_id, WORKSPACEID new_id, CallbackFun on_complete = nullptr) = 0;
    virtual void on_move_swipe(Vector2D delta);
    virtual WORKSPACEID on_move_swipe_end();

    virtual WORKSPACEID get_ws_id_in_direction(int x, int y, std::string& direction);

    virtual bool on_mouse_axis(double delta);

    virtual bool should_manage_mouse();
    virtual bool should_render_window(PHLWINDOW window);
    virtual float drag_window_scale();
    virtual void init_position();
    virtual void build_overview_layout(HTViewStage stage);
    virtual void render();

    void post_render();

    PHLMONITOR get_monitor();
    bool is_monitor_workspace(PHLWORKSPACE workspace);
    std::vector<PHLWORKSPACE> get_monitor_workspaces();
    PHLWORKSPACE get_workspace_from_layout(WORKSPACEID workspace_id);
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
