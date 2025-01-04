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

    void build_overview_layout(int override = 0);

    // Workspace that the overview was opened from
    PHLWORKSPACEREF ori_workspace;

    WORKSPACEID get_exit_workspace_id(bool override_hover);

    // Override = -1 if not active, 0 if use anims, 1 if final, hacky but whatever
    // NOTE: if -1 then, we calculate the boxes as if 0, 0 was the active ws
    CBox calculate_ws_box(int x, int y, int override);

  public:
    HTView(MONITORID in_monitor_id);

    bool is_active();
    bool is_closing();
    bool is_navigating();

    PHLMONITOR get_monitor();

    void show();
    void hide();
    void render();

    CAnimatedVariable<Vector2D> offset;
    CAnimatedVariable<float> scale;

    // arg is up, down, left, right;
    void move(std::string arg);

    // Use to switch to the proper workspace depending on behavior before
    // exiting. If overrideHover, we pref hover first over all else
    void do_exit_behavior(bool override_hover = false);

    // If return value < WORKSPACEID, then there is nothing there
    WORKSPACEID get_ws_id_from_global(Vector2D pos);

    CBox get_global_window_box(PHLWINDOW window, WORKSPACEID workspace_id);

    Vector2D global_to_local_ws_scaled(Vector2D pos, WORKSPACEID workspace_id);
    Vector2D global_to_local_ws_unscaled(Vector2D pos, WORKSPACEID workspace_id);
    Vector2D local_ws_scaled_to_global(Vector2D pos, WORKSPACEID workspace_id);
    Vector2D local_ws_unscaled_to_global(Vector2D pos, WORKSPACEID workspace_id);
};

typedef SP<HTView> PHTVIEW;
typedef WP<HTView> PHTVIEWREF;
