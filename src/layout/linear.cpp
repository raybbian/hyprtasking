#include "linear.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>
#include <ranges>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include "../config.hpp"
#include "../globals.hpp"
#include "../render.hpp"
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
    const float HEIGHT = HTConfig::value<Hyprlang::FLOAT>("linear:height") * monitor->m_scale;

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

    const float HEIGHT = HTConfig::value<Hyprlang::FLOAT>("linear:height") * monitor->m_scale;
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

    const float GAP_SIZE = HTConfig::value<Hyprlang::FLOAT>("gap_size") * monitor->m_scale;

    const PHLWORKSPACE new_ws = g_pCompositor->getWorkspaceByID(new_id);
    if (new_ws == nullptr)
        return;

    build_overview_layout(HT_VIEW_ANIMATING);

    const float cur_screen_min_x = overview_layout[new_id].box.x - GAP_SIZE;
    const float cur_screen_max_x =
        overview_layout[new_id].box.x + overview_layout[new_id].box.w + GAP_SIZE;

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

    const float GAP_SIZE = HTConfig::value<Hyprlang::FLOAT>("gap_size") * monitor->m_scale;

    const float total_ws_width =
        (overview_layout.size() * (GAP_SIZE + calculate_ws_box(0, 0, HT_VIEW_ANIMATING).w))
        + GAP_SIZE;

    // Stay at 0 if not long enough
    if (total_ws_width < monitor->m_transformedSize.x) {
        *scroll_offset = 0.;
        return true;
    }

    double new_offset = scroll_offset->goal()
        + delta * HTConfig::value<Hyprlang::FLOAT>("linear:scroll_speed") * -10.f;

    const float max_x = new_offset
        + (overview_layout.size() * (GAP_SIZE + calculate_ws_box(0, 0, HT_VIEW_ANIMATING).w))
        + GAP_SIZE;

    // Snap to left
    if (new_offset > 0.)
        new_offset = 0.;

    // Snap to right
    if (max_x < monitor->m_transformedSize.x)
        new_offset = new_offset + (monitor->m_transformedSize.x - max_x);

    *scroll_offset = new_offset;
    return true;
}

bool HTLayoutLinear::should_manage_mouse() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return 1;

    const float HEIGHT = HTConfig::value<Hyprlang::FLOAT>("linear:height") * monitor->m_scale;

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    CBox scaled_view_box = {
        Vector2D {0.f, monitor->m_transformedSize.y - view_offset->value()},
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

    if (window == g_pInputManager->m_currentlyDraggedWindow.lock())
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

    const float HEIGHT = HTConfig::value<Hyprlang::FLOAT>("linear:height") * monitor->m_scale;
    const float GAP_SIZE = HTConfig::value<Hyprlang::FLOAT>("gap_size") * monitor->m_scale;

    if (HEIGHT < 0 || HEIGHT > monitor->m_transformedSize.y)
        fail_exit("Linear layout height {} is taller than monitor size", HEIGHT);

    if (GAP_SIZE < 0 || GAP_SIZE > HEIGHT / 2.f)
        fail_exit("Invalid gap_size {} for linear layout", GAP_SIZE);

    float use_view_offset = view_offset->value();
    if (stage == HT_VIEW_CLOSED)
        use_view_offset = 0;
    else if (stage == HT_VIEW_OPENED)
        use_view_offset = HEIGHT;

    const float ws_height = HEIGHT - 2 * GAP_SIZE;
    const float ws_width = ws_height * monitor->m_transformedSize.x / monitor->m_transformedSize.y;

    const float ws_x = scroll_offset->value() + (x * (GAP_SIZE + ws_width) + GAP_SIZE);
    const float ws_y = monitor->m_transformedSize.y - use_view_offset + GAP_SIZE;
    return CBox {ws_x, ws_y, ws_width, ws_height};
}

void HTLayoutLinear::build_overview_layout(HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    overview_layout.clear();

   
    std::vector<WORKSPACEID> monitor_workspaces;
    for (auto& ws_ref : g_pCompositor->getWorkspaces()) {
        auto workspace = ws_ref.lock();
        if (!workspace)
            continue;
        if (workspace->m_monitor != monitor)
            continue;
        if (workspace->m_isSpecialWorkspace)
            continue;
        monitor_workspaces.push_back(workspace->m_id);
    }
    
    std::sort(monitor_workspaces.begin(), monitor_workspaces.end());

    WORKSPACEID big_id = monitor_workspaces.back();
    while (g_pCompositor->getWorkspaceByID(big_id) != nullptr)
        big_id++;
    monitor_workspaces.push_back(big_id);

    for (const auto& [x, ws_id] : monitor_workspaces | std::views::enumerate) {
        CBox ws_box = calculate_ws_box(x, 0, stage);
        overview_layout[ws_id] = {x, 0, ws_box};
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

    static auto PACTIVECOL = CConfigValue<Hyprlang::CUSTOMTYPE>("general:col.active_border");
    static auto PINACTIVECOL = CConfigValue<Hyprlang::CUSTOMTYPE>("general:col.inactive_border");

    auto* const ACTIVECOL = (CGradientValueData*)(PACTIVECOL.ptr())->getData();
    auto* const INACTIVECOL = (CGradientValueData*)(PINACTIVECOL.ptr())->getData();

    const float BORDERSIZE = HTConfig::value<Hyprlang::FLOAT>("border_size");
    const float HEIGHT = HTConfig::value<Hyprlang::FLOAT>("linear:height") * monitor->m_scale;

    timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);

    g_pHyprRenderer->damageMonitor(monitor);
    g_pHyprOpenGL->m_renderData.pCurrentMonData->blurFBShouldRender = true;

    // Do a dance with active workspaces: Hyprland will only properly render the
    // current active one so make the workspace active before rendering it, etc
    const PHLWORKSPACE start_workspace = monitor->m_activeWorkspace;
    g_pDesktopAnimationManager->startAnimation(start_workspace, CDesktopAnimationManager::ANIMATION_TYPE_OUT, false, true);
    start_workspace->m_visible = false;

    const PHLWORKSPACE big_ws = monitor->m_activeWorkspace;

    rendering_standard_ws = true;
    monitor->m_activeWorkspace = big_ws;
    g_pDesktopAnimationManager->startAnimation(big_ws, CDesktopAnimationManager::ANIMATION_TYPE_IN, false, true);
    big_ws->m_visible = true;

    // use pixel size for geometry
    CBox mon_box = {{0, 0}, monitor->m_pixelSize};
    // Render the current workspace on the screen
    ((render_workspace_t)(render_workspace_hook->m_original))(
        g_pHyprRenderer.get(),
        monitor,
        big_ws,
        &time,
        mon_box
    );

    // add blur/dim over the original workspace
    CRectPassElement::SRectData blur_data;
    blur_data.color = CHyprColor(0, 0, 0, dim_opacity->value());
    blur_data.box = mon_box;
    blur_data.blur = (bool)HTConfig::value<Hyprlang::INT>("linear:blur");
    blur_data.blurA = blur_strength->value();
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(blur_data));

    g_pDesktopAnimationManager->startAnimation(big_ws, CDesktopAnimationManager::ANIMATION_TYPE_OUT, false, true);
    big_ws->m_visible = false;
    rendering_standard_ws = false;

    CBox view_box = {
        {0.f, monitor->m_transformedSize.y - view_offset->value()},
        {(float)monitor->m_transformedSize.x, (float)HEIGHT}
    };

    CRectPassElement::SRectData data;
    data.color = CHyprColor {HTConfig::value<Hyprlang::INT>("bg_color")}.stripA();
    data.box = view_box;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));

    build_overview_layout(HT_VIEW_ANIMATING);

    CBox global_mon_box = {monitor->m_position, monitor->m_transformedSize};
    for (const auto& [ws_id, ws_layout] : overview_layout) {
        // Could be nullptr, in which we render only layers
        const PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(ws_id);

        // renderModif translation used by renderWorkspace is weird so need
        // to scale the translation up as well. Geometry is also calculated from pixel size and not transformed size??
        CBox render_box = {
            {ws_layout.box.pos() / (ws_layout.box.w / monitor->m_transformedSize.x)},
            ws_layout.box.size()
        };
        if (monitor->m_transform % 2 == 1)
            std::swap(render_box.w, render_box.h);

        CBox global_box = {ws_layout.box.pos() + monitor->m_position, ws_layout.box.size()};
        if (global_box.intersection(global_mon_box).empty())
            continue;

        const CGradientValueData border_col = workspace == big_ws ? *ACTIVECOL : *INACTIVECOL;
        CBox border_box = ws_layout.box;

        CBorderPassElement::SBorderData data;
        data.box = border_box;
        data.grad1 = border_col;
        data.borderSize = BORDERSIZE;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(data));

        if (workspace != nullptr) {
            monitor->m_activeWorkspace = workspace;
            g_pDesktopAnimationManager->startAnimation(workspace, CDesktopAnimationManager::ANIMATION_TYPE_IN, false, true);
            workspace->m_visible = true;

            ((render_workspace_t)(render_workspace_hook->m_original))(
                g_pHyprRenderer.get(),
                monitor,
                workspace,
                &time,
                render_box
            );

            g_pDesktopAnimationManager->startAnimation(workspace, CDesktopAnimationManager::ANIMATION_TYPE_OUT, false, true);
            workspace->m_visible = false;
        } else {
            // If pWorkspace is null, then just render the layers
            ((render_workspace_t)(render_workspace_hook->m_original))(
                g_pHyprRenderer.get(),
                monitor,
                workspace,
                &time,
                render_box

            );
        }
    }

    monitor->m_activeWorkspace = start_workspace;
    g_pDesktopAnimationManager->startAnimation(start_workspace, CDesktopAnimationManager::ANIMATION_TYPE_IN, false, true);
    start_workspace->m_visible = true;

    // Render dragged window at mouse cursor
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;
    const PHLWINDOW dragged_window = g_pInputManager->m_currentlyDraggedWindow.lock();
    if (dragged_window == nullptr)
        return;
    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const CBox window_box = dragged_window->getWindowMainSurfaceBox()
                                .translate(-mouse_coords)
                                .scale(cursor_view->layout->drag_window_scale())
                                .translate(mouse_coords);
    if (!window_box.intersection(monitor->logicalBox()).empty())
        render_window_at_box(dragged_window, monitor, &time, window_box);
}
