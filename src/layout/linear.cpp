#include "linear.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>
#include <ranges>

#include "../config.hpp"
#include "../globals.hpp"
#include "layout_base.hpp"

using Hyprutils::Utils::CScopeGuard;

HTLayoutLinear::HTLayoutLinear(VIEWID new_view_id) : HTLayoutBase(new_view_id) {
    g_pAnimationManager->createAnimation(
        0.f,
        scroll_offset,
        g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
        AVARDAMAGE_NONE
    );
    g_pAnimationManager->createAnimation(
        0.f,
        view_offset,
        g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
        AVARDAMAGE_NONE
    );
    g_pAnimationManager->createAnimation(
        0.f,
        blur_strength,
        g_pConfigManager->getAnimationPropertyConfig("fadeIn"),
        AVARDAMAGE_NONE
    );
    g_pAnimationManager->createAnimation(
        0.f,
        dim_opacity,
        g_pConfigManager->getAnimationPropertyConfig("fadeDim"),
        AVARDAMAGE_NONE
    );

    init_position();
}

std::string HTLayoutLinear::layout_name() {
    return "linear";
}

void HTLayoutLinear::close_open_lerp(float perc) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const float HEIGHT = HTConfig::value_float("linear:height") * monitor->m_scale;

    view_offset->resetAllCallbacks();
    blur_strength->resetAllCallbacks();
    dim_opacity->resetAllCallbacks();
    view_offset->setValueAndWarp(std::lerp(0.0, HEIGHT, perc));
    blur_strength->setValueAndWarp(std::lerp(0.0, 2.0, perc));
    dim_opacity->setValueAndWarp(std::lerp(0.0, 0.4, perc));
}

void HTLayoutLinear::on_show(CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            view_offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const float HEIGHT = HTConfig::value_float("linear:height") * monitor->m_scale;
    *view_offset = HEIGHT;
    *blur_strength = 2.0;
    *dim_opacity = 0.4;
}

void HTLayoutLinear::on_hide(CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            view_offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    *view_offset = 0;
    *blur_strength = 0;
    *dim_opacity = 0;
}

void HTLayoutLinear::on_move(WORKSPACEID old_id, WORKSPACEID new_id, CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            scroll_offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const float GAP_SIZE = HTConfig::value_float("gap_size") * monitor->m_scale;

    build_overview_layout(HT_VIEW_ANIMATING);

    const auto target_it = overview_layout.find(new_id);
    if (target_it == overview_layout.end() || get_workspace_from_layout(new_id) == nullptr)
        return;

    const float cur_screen_min_x = target_it->second.box.x - GAP_SIZE;
    const float cur_screen_max_x =
        target_it->second.box.x + target_it->second.box.w + GAP_SIZE;

    if (cur_screen_min_x < 0) {
        *scroll_offset = scroll_offset->value() - cur_screen_min_x;
    } else if (cur_screen_max_x > monitor->m_transformedSize.x) {
        *scroll_offset = scroll_offset->value() - (cur_screen_max_x - monitor->m_transformedSize.x);
    }
}

bool HTLayoutLinear::on_mouse_axis(double delta) {
    if (!should_manage_mouse())
        return false;

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return false;

    const float GAP_SIZE = HTConfig::value_float("gap_size") * monitor->m_scale;

    const float total_ws_width =
        (overview_layout.size() * (GAP_SIZE + calculate_ws_box(0, 0, HT_VIEW_ANIMATING).w))
        + GAP_SIZE;

    if (total_ws_width < monitor->m_transformedSize.x) {
        *scroll_offset = 0.;
        return true;
    }

    double new_offset = scroll_offset->goal()
        + delta * HTConfig::value_float("linear:scroll_speed") * -10.f;

    const float max_x = new_offset
        + (overview_layout.size() * (GAP_SIZE + calculate_ws_box(0, 0, HT_VIEW_ANIMATING).w))
        + GAP_SIZE;

    if (new_offset > 0.)
        new_offset = 0.;

    if (max_x < monitor->m_transformedSize.x)
        new_offset = new_offset + (monitor->m_transformedSize.x - max_x);

    *scroll_offset = new_offset;
    return true;
}

const float calculate_y(float size_y, float offset_value, float max_offset) {
    const bool top = HTConfig::value("linear:top");
    if (top)
        return offset_value - max_offset;
    return size_y - offset_value;
}

bool HTLayoutLinear::should_manage_mouse() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return 1;

    const float HEIGHT = HTConfig::value_float("linear:height") * monitor->m_scale;

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    CBox scaled_view_box = {
        Vector2D {0.f, calculate_y(monitor->m_transformedSize.y, view_offset->value(), HEIGHT)},
        {(float)monitor->m_transformedSize.x, (float)HEIGHT}
    };

    return scaled_view_box.scale(1 / monitor->m_scale)
        .translate(monitor->m_position)
        .containsPoint(mouse_coords);
}

bool HTLayoutLinear::should_render_window(PHLWINDOW window) {
    bool ori_result = HTLayoutBase::should_render_window(window);

    const PHLMONITOR monitor = get_monitor();
    if (window == nullptr || monitor == nullptr)
        return ori_result;

    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target != nullptr && window == target->window())
        return false;

    if (rendering_standard_ws)
        return ori_result;

    PHLWORKSPACE workspace = window->m_workspace;
    if (workspace == nullptr)
        return false;

    CBox window_box = get_global_window_box(window, window->workspaceID());
    if (window_box.empty())
        return false;
    if (window_box.intersection(monitor->logicalBox()).empty())
        return false;

    return ori_result;
}

float HTLayoutLinear::drag_window_scale() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return 1;

    if (should_manage_mouse())
        return calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x;

    return 1;
}

void HTLayoutLinear::init_position() {
    build_overview_layout(HT_VIEW_CLOSED);

    scroll_offset->setValueAndWarp(0);
    view_offset->setValueAndWarp(0);
}

CBox HTLayoutLinear::calculate_ws_box(int x, int y, HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    if (monitor->m_transformedSize.x < 1 || monitor->m_transformedSize.y < 1)
        return {};

    const float HEIGHT = HTConfig::value_float("linear:height") * monitor->m_scale;
    const float GAP_SIZE = HTConfig::value_float("gap_size") * monitor->m_scale;

    if (HEIGHT < 0 || HEIGHT > monitor->m_transformedSize.y)
        return {};

    if (GAP_SIZE < 0 || GAP_SIZE > HEIGHT / 2.f)
        return {};

    float use_view_offset = view_offset->value();
    if (stage == HT_VIEW_CLOSED)
        use_view_offset = 0;
    else if (stage == HT_VIEW_OPENED)
        use_view_offset = HEIGHT;

    const float ws_height = HEIGHT - 2 * GAP_SIZE;
    const float ws_width = ws_height * monitor->m_transformedSize.x / monitor->m_transformedSize.y;

    const float ws_x = scroll_offset->value() + (x * (GAP_SIZE + ws_width) + GAP_SIZE);
    const float ws_y = calculate_y(monitor->m_transformedSize.y, use_view_offset, HEIGHT) + GAP_SIZE;
    return CBox {ws_x, ws_y, ws_width, ws_height};
}

void HTLayoutLinear::build_overview_layout(HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    overview_layout.clear();

    const std::vector<PHLWORKSPACE> monitor_workspaces = get_monitor_workspaces();
    for (const auto& [x, workspace] : monitor_workspaces | std::views::enumerate) {
        if (workspace == nullptr)
            continue;
        CBox ws_box = calculate_ws_box(x, 0, stage);
        overview_layout[workspace->m_id] = {
            (int)x,
            0,
            ws_box,
            workspace->m_id,
            workspace->m_name,
            monitor->m_id,
        };
    }
}

void HTLayoutLinear::render() {
    HTLayoutBase::render();
    CScopeGuard x([this] { post_render(); });

    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr)
        return;
    const PHLMONITOR monitor = par_view->get_monitor();
    if (monitor == nullptr)
        return;

    const int bg_color = HTConfig::value("bg_color");
    const int blur_enabled = HTConfig::value("linear:blur");
    const float HEIGHT = HTConfig::value_float("linear:height") * monitor->m_scale;


    g_pHyprRenderer->damageMonitor(monitor);
    g_pHyprOpenGL->m_renderData.pCurrentMonData->blurFBShouldRender = true;

    const PHLWORKSPACE start_workspace = monitor->m_activeWorkspace;
    if (start_workspace == nullptr)
        return;

    CScopeGuard restore_workspace([this, &monitor, &start_workspace] {
        rendering_standard_ws = false;
        if (monitor == nullptr || start_workspace == nullptr)
            return;
        monitor->m_activeWorkspace = start_workspace;
        g_pDesktopAnimationManager->startAnimation(
            start_workspace,
            CDesktopAnimationManager::ANIMATION_TYPE_IN,
            false,
            true
        );
        start_workspace->m_visible = true;
    });

    g_pDesktopAnimationManager->startAnimation(
        start_workspace,
        CDesktopAnimationManager::ANIMATION_TYPE_OUT,
        false,
        true
    );
    start_workspace->m_visible = false;

    const PHLWORKSPACE big_ws = monitor->m_activeWorkspace;

    rendering_standard_ws = true;
    monitor->m_activeWorkspace = big_ws;
    g_pDesktopAnimationManager->startAnimation(
        start_workspace,
        CDesktopAnimationManager::ANIMATION_TYPE_IN,
        false,
        true
    );
    big_ws->m_visible = true;

    CBox mon_box = {{0, 0}, monitor->m_pixelSize};
    render_workspace(big_ws, mon_box, true);

    CRectPassElement::SRectData blur_data;
    blur_data.color = CHyprColor(0, 0, 0, dim_opacity->value());
    blur_data.box = mon_box;
    blur_data.blur = (bool)blur_enabled;
    blur_data.blurA = blur_strength->value();
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(blur_data));

    g_pDesktopAnimationManager->startAnimation(
        start_workspace,
        CDesktopAnimationManager::ANIMATION_TYPE_OUT,
        false,
        true
    );
    big_ws->m_visible = false;
    rendering_standard_ws = false;

    CBox view_box = {
        {0.f, calculate_y(monitor->m_transformedSize.y, view_offset->value(), HEIGHT)},
        {(float)monitor->m_transformedSize.x, (float)HEIGHT}
    };

    CRectPassElement::SRectData data;
    data.color = CHyprColor {bg_color}.stripA();
    data.box = view_box;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));

    build_overview_layout(HT_VIEW_ANIMATING);

    CBox global_mon_box = {monitor->m_position, monitor->m_transformedSize};
    for (const auto& [ws_id, ws_layout] : overview_layout) {
        const PHLWORKSPACE workspace = get_workspace_from_layout(ws_id);

        CBox render_box = {
            {ws_layout.box.pos() / (ws_layout.box.w / monitor->m_transformedSize.x)},
            ws_layout.box.size()
        };
        if (monitor->m_transform % 2 == 1)
            std::swap(render_box.w, render_box.h);

        CBox global_box = {ws_layout.box.pos() + monitor->m_position, ws_layout.box.size()};
        if (global_box.intersection(global_mon_box).empty())
            continue;

        render_border(ws_layout.box, workspace == big_ws);

        render_workspace(workspace, render_box, false);
    }

    monitor->m_activeWorkspace = start_workspace;
    g_pDesktopAnimationManager->startAnimation(
        start_workspace,
        CDesktopAnimationManager::ANIMATION_TYPE_IN,
        false,
        true
    );
    start_workspace->m_visible = true;

    render_dragged_window();
}
