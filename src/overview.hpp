#pragma once

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

struct HTView {
  private:
    MONITORID monitor_id;

    bool closing;
    bool active;
    bool navigating;

    struct HTWorkspace {
        int row;
        int col;
        CBox box;
    };

    // Store the bounding boxes of each workspace as rendered. Modified on
    // render and accessed during mouse button events.
    // TODO: is there a better way to do this?
    // NOTE: workspace boxes do not consider monitor scaling
    std::unordered_map<WORKSPACEID, HTWorkspace> overview_layout;

    void build_overview_layout(bool use_anim_modifs = true);

    CAnimatedVariable<Vector2D> offset;
    CAnimatedVariable<float> scale;

    // Workspace that the overview was opened from
    PHLWORKSPACEREF ori_workspace;

    bool try_switch_to_hover();
    bool try_switch_to_original();

    Vector2D gaps();

  public:
    HTView(MONITORID in_monitor_id);

    bool is_active();
    bool is_closing();
    bool is_navigating();

    PHLMONITOR get_monitor();

    void show();
    void hide();
    void render();

    // arg is up, down, left, right;
    void move(std::string arg);

    // Use to switch to the proper workspace depending on behavior before
    // exiting. If overrideHover, we pref hover first over all else
    void do_exit_behavior(bool override_hover = false);

    // If return value < WORKSPACEID, then there is nothing there
    WORKSPACEID get_ws_id_from_global(Vector2D pos);

    CBox get_global_ws_box_from_id(WORKSPACEID workspace_id);

    CBox get_global_window_box(PHLWINDOW window);

    Vector2D global_pos_to_ws_global(Vector2D pos, WORKSPACEID workspace_id);
    Vector2D ws_global_pos_to_global(Vector2D pos, WORKSPACEID workspace_id);
};

typedef SP<HTView> PHTVIEW;
typedef WP<HTView> PHTVIEWREF;
