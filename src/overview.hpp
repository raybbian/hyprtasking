#pragma once

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "layout/layout_base.hpp"

typedef long VIEWID;

class HTView {
  private:
    bool closing;
    bool active;
    bool navigating;

    // Workspace that the overview was opened from
    PHLWORKSPACEREF ori_workspace;

  public:
    HTView(MONITORID in_monitor_id);

    MONITORID monitor_id;

    SP<HTLayoutBase> layout;

    WORKSPACEID get_exit_workspace_id(bool override_hover);

    bool is_active();
    bool is_closing();
    bool is_navigating();

    PHLMONITOR get_monitor();

    void show();
    void hide();

    // arg is up, down, left, right;
    void move(std::string arg);

    // Use to switch to the proper workspace depending on behavior before
    // exiting. If override_hover, we pref hover first over all else
    void do_exit_behavior(bool override_hover = false);
};

typedef SP<HTView> PHTVIEW;
typedef WP<HTView> PHTVIEWREF;
