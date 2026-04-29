#pragma once

#include <hyprland/src/helpers/AnimatedVariable.hpp>

#include "../types.hpp"
#include "layout_base.hpp"

class HTLayoutGrid: public HTLayoutBase {
  private:
    PHLANIMVAR<float> scale;
    PHLANIMVAR<Vector2D> offset;

    int get_effective_layer_count(size_t workspace_count);
    int get_workspace_layer(WORKSPACEID workspace_id);

  public:
    HTLayoutGrid(VIEWID view_id);
    virtual ~HTLayoutGrid() = default;

    std::unordered_map<WORKSPACEID, int> pinned_positions;

    void pin_workspace_to_slot(WORKSPACEID ws_id, int slot);
    void unpin_workspace(WORKSPACEID ws_id);

    std::tuple<int, int, int> get_grid_cell_from_global(Vector2D pos);

    virtual std::string layout_name();

    virtual CBox calculate_ws_box(int x, int y, HTViewStage stage);

    virtual void close_open_lerp(float perc);
    virtual void on_show(CallbackFun on_complete);
    virtual void on_hide(CallbackFun on_complete);
    virtual void on_move(WORKSPACEID old_id, WORKSPACEID new_id, CallbackFun on_complete);
    virtual void on_move_swipe(Vector2D delta);
    virtual WORKSPACEID on_move_swipe_end();

    virtual WORKSPACEID get_ws_id_in_direction(int x, int y, std::string& direction);

    virtual bool should_render_window(PHLWINDOW window);
    virtual float drag_window_scale();
    virtual void init_position();
    virtual void build_overview_layout(HTViewStage stage);
    virtual void render();
};