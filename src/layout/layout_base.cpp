#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
// Hyprland exposes render pass internals privately; this targeted workaround is
// still required for CRenderPass::m_passElements cleanup in supported versions.
#define private public
#include <hyprland/src/render/Renderer.hpp>
#undef private
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>

#include "../config.hpp"
#include "../globals.hpp"
#include "../pass/pass_element.hpp"
#include "../render.hpp"
#include "../types.hpp"
#include "layout_base.hpp"

HTLayoutBase::HTLayoutBase(VIEWID new_view_id)
    : view_id(new_view_id) {
}

void HTLayoutBase::on_move_swipe(Vector2D delta) {
}

WORKSPACEID HTLayoutBase::on_move_swipe_end() {
    return WORKSPACE_INVALID;
}

WORKSPACEID HTLayoutBase::get_ws_id_in_direction(int x, int y, std::string& direction) {
    if (direction == "up") {
        y--;
    } else if (direction == "down") {
        y++;
    } else if (direction == "right") {
        x++;
    } else if (direction == "left") {
        x--;
    } else {
        return WORKSPACE_INVALID;
    }
    return get_ws_id_from_xy(x, y);
}

bool HTLayoutBase::on_mouse_axis(double delta) {
    return false;
}

bool HTLayoutBase::should_manage_mouse() {
    return true;
}

bool HTLayoutBase::should_render_window(PHLWINDOW window) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr || window == nullptr)
        return false;

    return ((should_render_window_t)(should_render_window_hook->m_original))(
        g_pHyprRenderer.get(),
        window,
        monitor
    );
}

float HTLayoutBase::drag_window_scale() {
    return 1.f;
}

void HTLayoutBase::init_position() {
}

void HTLayoutBase::build_overview_layout(HTViewStage stage) {
}

void HTLayoutBase::render() {
    update_render_cache();

    CClearPassElement::SClearData data;
    data.color = CHyprColor {0};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CClearPassElement>(data));
}

void HTLayoutBase::update_render_cache() {
    cached_border_size = HTConfig::value_float("border_size");
}

void HTLayoutBase::render_workspace(PHLWORKSPACE ws, CBox render_box, bool is_active) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    if (ws != nullptr) {
        monitor->m_activeWorkspace = ws;
        g_pDesktopAnimationManager->startAnimation(
            ws,
            CDesktopAnimationManager::ANIMATION_TYPE_IN,
            false,
            true
        );
        ws->m_visible = true;
    }

    ((render_workspace_t)(render_workspace_hook->m_original))(
        g_pHyprRenderer.get(),
        monitor,
        ws,
        Time::steadyNow(),
        render_box
    );

    if (ws == nullptr || is_active)
        return;

    g_pDesktopAnimationManager->startAnimation(
        ws,
        CDesktopAnimationManager::ANIMATION_TYPE_OUT,
        false,
        true
    );
    ws->m_visible = false;
}

void HTLayoutBase::render_dragged_window() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;

    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target == nullptr)
        return;

    const PHLWINDOW dragged_window = target->window();
    if (dragged_window == nullptr)
        return;

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const CBox window_box = dragged_window->getWindowMainSurfaceBox()
                                .translate(-mouse_coords)
                                .scale(cursor_view->layout->drag_window_scale())
                                .translate(mouse_coords);
    if (!window_box.intersection(monitor->logicalBox()).empty())
        render_window_at_box(dragged_window, monitor, Time::steadyNow(), window_box);
}

void HTLayoutBase::render_border(CBox box, bool is_active) {
    static auto PACTIVECOL = CConfigValue<Hyprlang::CUSTOMTYPE>("general:col.active_border");
    static auto PINACTIVECOL = CConfigValue<Hyprlang::CUSTOMTYPE>("general:col.inactive_border");

    auto* const active_col = (CGradientValueData*)(PACTIVECOL.ptr())->getData();
    auto* const inactive_col = (CGradientValueData*)(PINACTIVECOL.ptr())->getData();

    CBorderPassElement::SBorderData data;
    data.box = box;
    data.grad1 = is_active ? *active_col : *inactive_col;
    data.borderSize = cached_border_size;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(data));
}

const std::string CLEAR_PASS_ELEMENT_NAME = "CClearPassElement";

void HTLayoutBase::post_render() {
    bool first = true;
    std::erase_if(g_pHyprRenderer->m_renderPass.m_passElements, [&first](const auto& e) {
        bool res = e->element->passName() == CLEAR_PASS_ELEMENT_NAME && !first;
        first = false;
        return res;
    });
    g_pHyprRenderer->m_renderPass.add(makeUnique<HTPassElement>());
}

PHLMONITOR HTLayoutBase::get_monitor() {
    if (par_view == nullptr && ht_manager != nullptr)
        par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr)
        return nullptr;
    return par_view->get_monitor();
}

bool HTLayoutBase::is_monitor_workspace(PHLWORKSPACE workspace) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr || workspace == nullptr)
        return false;
    if (workspace->inert() || workspace->m_isSpecialWorkspace)
        return false;
    return workspace->monitorID() == monitor->m_id;
}

std::vector<PHLWORKSPACE> HTLayoutBase::get_monitor_workspaces() {
    std::vector<PHLWORKSPACE> workspaces;
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return workspaces;

    for (PHLWORKSPACE workspace : g_pCompositor->getWorkspacesCopy()) {
        if (!is_monitor_workspace(workspace))
            continue;
        workspaces.push_back(workspace);
    }
    return workspaces;
}

PHLWORKSPACE HTLayoutBase::get_workspace_from_layout(WORKSPACEID workspace_id) {
    if (!overview_layout.contains(workspace_id))
        return nullptr;

    PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(workspace_id);
    if (!is_monitor_workspace(workspace))
        return nullptr;
    return workspace;
}

WORKSPACEID HTLayoutBase::get_ws_id_from_global(Vector2D pos) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return WORKSPACE_INVALID;

    if (!monitor->logicalBox().containsPoint(pos))
        return WORKSPACE_INVALID;

    Vector2D relative_pos = (pos - monitor->m_position) * monitor->m_scale;
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

    const PHLWORKSPACE workspace = get_workspace_from_layout(workspace_id);
    if (workspace == nullptr)
        return {};

    const CBox ws_window_box = window->getWindowMainSurfaceBox();

    const Vector2D top_left =
        local_ws_unscaled_to_global(ws_window_box.pos() - monitor->m_position, workspace->m_id);
    const Vector2D bottom_right = local_ws_unscaled_to_global(
        ws_window_box.pos() + ws_window_box.size() - monitor->m_position,
        workspace->m_id
    );

    return {top_left, bottom_right - top_left};
}

CBox HTLayoutBase::get_global_ws_box(WORKSPACEID workspace_id) {
    const auto it = overview_layout.find(workspace_id);
    if (it == overview_layout.end())
        return {};

    const CBox scaled_ws_box = it->second.box;
    const Vector2D top_left = local_ws_scaled_to_global(scaled_ws_box.pos(), workspace_id);
    const Vector2D bottom_right =
        local_ws_scaled_to_global(scaled_ws_box.pos() + scaled_ws_box.size(), workspace_id);
    return {top_left, bottom_right - top_left};
}

Vector2D HTLayoutBase::global_to_local_ws_unscaled(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    const auto it = overview_layout.find(workspace_id);
    if (it == overview_layout.end())
        return {};

    CBox workspace_box = it->second.box;
    if (workspace_box.empty())
        return {};
    pos -= monitor->m_position;
    pos *= monitor->m_scale;
    pos -= workspace_box.pos();
    pos /= monitor->m_scale;
    pos /= workspace_box.w / monitor->m_transformedSize.x;
    return pos;
}

Vector2D HTLayoutBase::global_to_local_ws_scaled(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    pos = global_to_local_ws_unscaled(pos, workspace_id);
    pos *= monitor->m_scale;
    return pos;
}

Vector2D HTLayoutBase::local_ws_unscaled_to_global(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    const auto it = overview_layout.find(workspace_id);
    if (it == overview_layout.end())
        return {};

    CBox workspace_box = it->second.box;
    if (workspace_box.empty())
        return {};
    pos *= workspace_box.w / monitor->m_transformedSize.x;
    pos *= monitor->m_scale;
    pos += workspace_box.pos();
    pos /= monitor->m_scale;
    pos += monitor->m_position;
    return pos;
}

Vector2D HTLayoutBase::local_ws_scaled_to_global(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    pos /= monitor->m_scale;
    return local_ws_unscaled_to_global(pos, workspace_id);
}
