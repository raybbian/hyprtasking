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
  public:
    bool closing;
    bool active;
    bool navigating;

    HTView(MONITORID in_monitor_id);

    void change_layout(const std::string& layout_name);

    MONITORID monitor_id;

    SP<HTLayoutBase> layout;

    void do_exit_behavior(bool exit_on_mouse);

    PHLMONITOR get_monitor();

    void show();
    void hide(bool exit_on_mouse);

    // arg is up, down, left, right;
    void move(std::string arg, bool move_window);
};

typedef SP<HTView> PHTVIEW;
typedef WP<HTView> PHTVIEWREF;
