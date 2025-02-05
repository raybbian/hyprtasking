#pragma once

#include "../types.hpp"
#include "layout_base.hpp"

class HTLayoutLinear: public HTLayoutBase {
  private:
    PHLANIMVAR<float> scroll_offset;
    PHLANIMVAR<float> view_offset;
    PHLANIMVAR<float> blur_strength;
    PHLANIMVAR<float> dim_opacity;

    bool rendering_standard_ws;

    CBox calculate_ws_box(int x, int y, HTViewStage stage);

  public:
    HTLayoutLinear(VIEWID view_id);
    virtual ~HTLayoutLinear() = default;

    virtual std::string layout_name();

    virtual void close_open_lerp(float perc);
    virtual void on_show(CallbackFun on_complete);
    virtual void on_hide(CallbackFun on_complete);
    virtual void on_move(WORKSPACEID old_id, WORKSPACEID new_id, CallbackFun on_complete);

    virtual bool on_mouse_axis(double delta);

    virtual bool should_manage_mouse();
    virtual bool should_render_window(PHLWINDOW window);
    virtual float drag_window_scale();
    virtual void init_position();
    virtual void build_overview_layout(HTViewStage stage);
    virtual void render();
};
