#pragma once

#include <hyprland/src/helpers/AnimatedVariable.hpp>

#include "../types.hpp"
#include "layout_base.hpp"

class HTLayoutGrid: public HTLayoutBase {
  private:
    PHLANIMVAR<float> scale;
    PHLANIMVAR<Vector2D> offset;

    CBox calculate_ws_box(int x, int y, HTViewStage stage);

  public:
    HTLayoutGrid(VIEWID view_id);
    virtual ~HTLayoutGrid() = default;

    virtual std::string layout_name();

    virtual void close_open_lerp(float perc);
    virtual void on_show(CallbackFun on_complete);
    virtual void on_hide(CallbackFun on_complete);
    virtual void on_move(WORKSPACEID old_id, WORKSPACEID new_id, CallbackFun on_complete);

    virtual WORKSPACEID get_ws_id_in_direction(int x, int y, std::string& direction);

    virtual bool should_render_window(PHLWINDOW window);
    virtual float drag_window_scale();
    virtual void init_position();
    virtual void build_overview_layout(HTViewStage stage);
    virtual void render();
};
