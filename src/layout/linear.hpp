#pragma once

#include "../types.hpp"
#include "layout_base.hpp"

class HTLayoutLinear: public HTLayoutBase {
  private:
    CAnimatedVariable<float> scroll_offset;
    CAnimatedVariable<float> view_offset;

    bool rendering_standard_ws;

    CBox calculate_ws_box(int x, int y, HTViewStage stage);

  public:
    HTLayoutLinear(VIEWID view_id);
    virtual ~HTLayoutLinear() = default;

    virtual std::string layout_name();

    virtual void on_show(std::function<void(void* thisptr)> on_complete);
    virtual void on_hide(std::function<void(void* thisptr)> on_complete);
    virtual void
    on_move(WORKSPACEID old_id, WORKSPACEID new_id, std::function<void(void* thisptr)> on_complete);

    virtual bool should_manage_mouse();
    virtual bool should_render_window(PHLWINDOW window);
    virtual float drag_window_scale();
    virtual void init_position();
    virtual void build_overview_layout(HTViewStage stage);
    virtual void render();
};
