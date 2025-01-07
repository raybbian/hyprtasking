#pragma once

#include "../types.hpp"
#include "layout_base.hpp"

class HTLayoutLinear: public HTLayoutBase {
  private:
    CAnimatedVariable<float> scale;
    CAnimatedVariable<Vector2D> offset;

    CBox calculate_ws_box(int x, int y, HTViewStage stage);

  public:
    HTLayoutLinear(VIEWID view_id);
    virtual ~HTLayoutLinear() = default;

    virtual void on_show(std::function<void(void* thisptr)> on_complete);
    virtual void on_hide(std::function<void(void* thisptr)> on_complete);
    virtual void on_move(WORKSPACEID ws_id, std::function<void(void* thisptr)> on_complete);

    virtual float drag_window_scale();
    virtual void init_position();
    virtual void build_overview_layout(HTViewStage stage);
    virtual void render();
};
