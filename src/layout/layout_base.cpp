#include "layout_base.hpp"

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "../globals.hpp"

HTLayoutBase::HTLayoutBase(VIEWID new_view_id) : view_id(new_view_id) {
    ;
}

void HTLayoutBase::on_show(std::function<void(void* thisptr)> on_complete) {
    ;
}

void HTLayoutBase::on_hide(std::function<void(void* thisptr)> on_complete) {
    ;
}

void HTLayoutBase::on_move(WORKSPACEID ws_id, std::function<void(void* thisptr)> on_complete) {
    ;
}

float HTLayoutBase::drag_window_scale() {
    return 1.f;
}

void HTLayoutBase::init_position() {
    ;
}

void HTLayoutBase::render() {
    ;
}

void HTLayoutBase::build_overview_layout(HTViewStage stage) {
    ;
}

PHLMONITOR HTLayoutBase::get_monitor() {
    const auto par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr)
        return nullptr;
    return par_view->get_monitor();
}

WORKSPACEID HTLayoutBase::get_ws_id_from_global(Vector2D pos) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return WORKSPACE_INVALID;

    if (!monitor->logicalBox().containsPoint(pos))
        return WORKSPACE_INVALID;

    Vector2D relative_pos = (pos - monitor->vecPosition) * monitor->scale;
    for (const auto& [id, layout] : overview_layout)
        if (layout.box.containsPoint(relative_pos))
            return id;

    return WORKSPACE_INVALID;
}

WORKSPACEID HTLayoutBase::get_ws_id_from_xy(int x, int y) {
    for (const auto& [id, layout] : overview_layout)
        if (layout.x == x && layout.y == y)
            return id;

    return WORKSPACE_INVALID;
}

CBox HTLayoutBase::get_global_window_box(PHLWINDOW window, WORKSPACEID workspace_id) {
    if (window == nullptr)
        return {};

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(workspace_id);
    if (workspace == nullptr || workspace->m_pMonitor != monitor)
        return {};

    CBox ws_window_box = window->getWindowMainSurfaceBox();

    Vector2D top_left =
        local_ws_unscaled_to_global(ws_window_box.pos() - monitor->vecPosition, workspace->m_iID);
    Vector2D bottom_right = local_ws_unscaled_to_global(
        ws_window_box.pos() + ws_window_box.size() - monitor->vecPosition,
        workspace->m_iID
    );

    return {top_left, bottom_right - top_left};
}

Vector2D HTLayoutBase::global_to_local_ws_unscaled(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    CBox workspace_box = overview_layout[workspace_id].box;
    if (workspace_box.empty())
        return {};
    pos -= monitor->vecPosition;
    pos *= monitor->scale;
    pos -= workspace_box.pos();
    pos /= monitor->scale;
    pos /= workspace_box.w / monitor->vecTransformedSize.x;
    return pos;
}

Vector2D HTLayoutBase::global_to_local_ws_scaled(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    pos = global_to_local_ws_unscaled(pos, workspace_id);
    pos *= monitor->scale;
    return pos;
}

Vector2D HTLayoutBase::local_ws_unscaled_to_global(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    CBox workspace_box = overview_layout[workspace_id].box;
    if (workspace_box.empty())
        return {};
    pos *= workspace_box.w / monitor->vecTransformedSize.x;
    pos *= monitor->scale;
    pos += workspace_box.pos();
    pos /= monitor->scale;
    pos += monitor->vecPosition;
    return pos;
}

Vector2D HTLayoutBase::local_ws_scaled_to_global(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    pos /= monitor->scale;
    return local_ws_unscaled_to_global(pos, workspace_id);
}
