#pragma once

#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <unordered_map>
#include <unordered_set>

#include "../types.hpp"
#include "layout_base.hpp"

struct HTGridSlot {
    int layer;
    int x;
    int y;
};

class HTLayoutGrid: public HTLayoutBase {
  private:
    PHLANIMVAR<float> scale;
    PHLANIMVAR<Vector2D> offset;

    // Survives workspace destruction so a slot stays sticky for an empty ws.
    std::unordered_map<WORKSPACEID, HTGridSlot> ws_slot_cache;
    std::unordered_map<long long, WORKSPACEID> slot_ws_cache;

    static long long pack_slot(int layer, int x, int y);

  public:
    HTLayoutGrid(VIEWID view_id);
    virtual ~HTLayoutGrid() = default;

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
    virtual void render_to_fbs();
    virtual void render();

    void refresh_workspace_cache(const std::unordered_set<WORKSPACEID>& extra_off_limits = {});
    WORKSPACEID slot_workspace(int layer, int x, int y);

    const std::unordered_map<WORKSPACEID, HTGridSlot>& cache() const { return ws_slot_cache; }
};
