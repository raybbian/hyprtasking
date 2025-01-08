#include "grid.hpp"

#include <ctime>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "../config.hpp"
#include "../globals.hpp"
#include "../overview.hpp"
#include "../render.hpp"
#include "../types.hpp"

HTLayoutGrid::HTLayoutGrid(VIEWID new_view_id) : HTLayoutBase(new_view_id) {
    offset.create(
        {0, 0},
        g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
        AVARDAMAGE_NONE
    );
    scale.create(1.f, g_pConfigManager->getAnimationPropertyConfig("windowsMove"), AVARDAMAGE_NONE);

    init_position();
}

std::string HTLayoutGrid::layout_name() {
    return "grid";
}

void HTLayoutGrid::on_show(std::function<void(void* thisptr)> on_complete) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    build_overview_layout(HT_VIEW_OPENED);
    const CBox ws_box = overview_layout[monitor->activeWorkspaceID()].box;
    scale = ws_box.w / monitor->vecTransformedSize.x; // 1 / ROWS
    offset = {0, 0};

    if (on_complete != nullptr)
        offset.setCallbackOnEnd(on_complete);
}

void HTLayoutGrid::on_hide(std::function<void(void* thisptr)> on_complete) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    build_overview_layout(HT_VIEW_CLOSED);
    const CBox ws_box = overview_layout[monitor->activeWorkspaceID()].box;
    scale = 1.;
    offset = -ws_box.pos();

    if (on_complete != nullptr)
        offset.setCallbackOnEnd(on_complete);
}

void HTLayoutGrid::on_move(
    WORKSPACEID old_id,
    WORKSPACEID new_id,
    std::function<void(void* thisptr)> on_complete
) {
    // prevent the thing from animating
    g_pCompositor->getWorkspaceByID(old_id)->m_vRenderOffset.warp();
    g_pCompositor->getWorkspaceByID(new_id)->m_vRenderOffset.warp();

    build_overview_layout(HT_VIEW_CLOSED);
    offset = -overview_layout[new_id].box.pos();
    if (on_complete != nullptr)
        offset.setCallbackOnEnd(on_complete);
}

bool HTLayoutGrid::should_render_window(PHLWINDOW window) {
    bool ori_result = HTLayoutBase::should_render_window(window);

    const PHLMONITOR monitor = get_monitor();
    if (window == nullptr || monitor == nullptr)
        return ori_result;

    if (window == g_pInputManager->currentlyDraggedWindow.lock())
        return false;

    PHLWORKSPACE workspace = window->m_pWorkspace;
    if (workspace == nullptr)
        return false;

    CBox window_box = get_global_window_box(window, window->workspaceID());
    if (window_box.empty())
        return false;
    if (window_box.intersection(monitor->logicalBox()).empty())
        return false;

    return ori_result;
}

float HTLayoutGrid::drag_window_scale() {
    return scale.value();
}

void HTLayoutGrid::init_position() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    // NOTE: can only be called when scale = 1 (overview inactive)
    build_overview_layout(HT_VIEW_CLOSED);
    const CBox ws_box = overview_layout[monitor->activeWorkspaceID()].box;
    offset.setValueAndWarp(-ws_box.pos());
}

CBox HTLayoutGrid::calculate_ws_box(int x, int y, HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    const int ROWS = HTConfig::grid_rows();
    const double GAP_SIZE = HTConfig::gap_size() * monitor->scale;
    const Vector2D gaps = {GAP_SIZE, GAP_SIZE};

    if (GAP_SIZE > std::min(monitor->vecTransformedSize.x, monitor->vecTransformedSize.y)
        || GAP_SIZE < 0)
        fail_exit("Gap size {} induces invalid render dimensions", GAP_SIZE);

    double render_x = monitor->vecTransformedSize.x - gaps.x * (ROWS + 1);
    double render_y = monitor->vecTransformedSize.y - gaps.y * (ROWS + 1);
    const double mon_aspect = monitor->vecTransformedSize.x / monitor->vecTransformedSize.y;
    Vector2D start_offset {};

    // make correct aspect ratio
    if (render_y * mon_aspect > render_x) {
        start_offset.y = (render_y - render_x / mon_aspect) / 2.f;
        render_y = render_x / mon_aspect;
    } else if (render_x / mon_aspect > render_y) {
        start_offset.x = (render_x - render_y * mon_aspect) / 2.f;
        render_x = render_y * mon_aspect;
    }

    float use_scale = scale.value();
    Vector2D use_offset = offset.value();
    if (stage == HT_VIEW_CLOSED) {
        use_scale = 1;
        use_offset = Vector2D {0, 0};
    } else if (stage == HT_VIEW_OPENED) {
        use_scale = (render_x / ROWS) / monitor->vecTransformedSize.x;
        use_offset = Vector2D {0, 0};
    }

    const Vector2D ws_sz = monitor->vecTransformedSize * use_scale;
    return CBox {Vector2D {x, y} * (ws_sz + gaps) + gaps + use_offset + start_offset, ws_sz};
};

void HTLayoutGrid::build_overview_layout(HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const int ROWS = HTConfig::grid_rows();

    const PHLMONITOR last_monitor = g_pCompositor->m_pLastMonitor.lock();
    g_pCompositor->setActiveMonitor(monitor);

    overview_layout.clear();

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < ROWS; x++) {
            int ind = y * ROWS + x + 1;
            const WORKSPACEID ws_id = getWorkspaceIDNameFromString(std::format("r~{}", ind)).id;
            const CBox ws_box = calculate_ws_box(x, y, stage);
            overview_layout[ws_id] = HTWorkspace {x, y, ws_box};
        }
    }

    if (last_monitor != nullptr)
        g_pCompositor->setActiveMonitor(last_monitor);
}

void HTLayoutGrid::render() {
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
    auto const BORDERSIZE = HTConfig::border_size();

    timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);

    g_pHyprRenderer->damageMonitor(monitor);
    g_pHyprOpenGL->m_RenderData.pCurrentMonData->blurFBShouldRender = true;
    CBox monitor_box = {{0, 0}, monitor->vecTransformedSize};
    g_pHyprOpenGL->renderRect(&monitor_box, CHyprColor {HTConfig::bg_color()}.stripA());

    // Do a dance with active workspaces: Hyprland will only properly render the
    // current active one so make the workspace active before rendering it, etc
    const PHLWORKSPACE start_workspace = monitor->activeWorkspace;
    start_workspace->startAnim(false, false, true);
    start_workspace->m_bVisible = false;

    build_overview_layout(HT_VIEW_ANIMATING);

    const WORKSPACEID exit_workspace_id = par_view->get_exit_workspace_id(false);

    CBox global_mon_box = {monitor->vecPosition, monitor->vecTransformedSize};
    for (const auto& [ws_id, ws_layout] : overview_layout) {
        // Could be nullptr, in which we render only layers
        const PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(ws_id);

        // renderModif translation used by renderWorkspace is weird so need
        // to scale the translation up as well. Geometry is also calculated from pixel size and not transformed size??
        CBox render_box = {{ws_layout.box.pos() / scale.value()}, ws_layout.box.size()};
        if (monitor->transform % 2 == 1)
            std::swap(render_box.w, render_box.h);

        // render active one last
        if (workspace == start_workspace && start_workspace != nullptr)
            continue;

        CBox global_box = {ws_layout.box.pos() + monitor->vecPosition, ws_layout.box.size()};
        if (global_box.intersection(global_mon_box).empty())
            continue;

        const CGradientValueData border_col =
            exit_workspace_id == ws_id ? *ACTIVECOL : *INACTIVECOL;
        CBox border_box = ws_layout.box;

        g_pHyprOpenGL->renderBorder(&border_box, border_col, 0, BORDERSIZE);
        if (workspace != nullptr) {
            monitor->activeWorkspace = workspace;
            workspace->startAnim(true, false, true);
            workspace->m_bVisible = true;

            ((render_workspace_t)(render_workspace_hook->m_pOriginal))(
                g_pHyprRenderer.get(),
                monitor,
                workspace,
                &time,
                render_box
            );

            workspace->startAnim(false, false, true);
            workspace->m_bVisible = false;
        } else {
            // If pWorkspace is null, then just render the layers
            ((render_workspace_t)(render_workspace_hook->m_pOriginal))(
                g_pHyprRenderer.get(),
                monitor,
                workspace,
                &time,
                render_box
            );
        }
    }

    monitor->activeWorkspace = start_workspace;
    start_workspace->startAnim(true, false, true);
    start_workspace->m_bVisible = true;

    // Render active workspace
    if (start_workspace != nullptr) {
        CBox ws_box = overview_layout[start_workspace->m_iID].box;

        // renderModif translation used by renderWorkspace is weird so need
        // to scale the translation up as well. Geometry is also calculated from pixel size and not transformed size??
        CBox render_box = {{ws_box.pos() / scale.value()}, ws_box.size()};
        if (monitor->transform % 2 == 1)
            std::swap(render_box.w, render_box.h);

        const CGradientValueData border_col =
            exit_workspace_id == start_workspace->m_iID ? *ACTIVECOL : *INACTIVECOL;
        CBox border_box = ws_box;
        g_pHyprOpenGL->renderBorder(&border_box, border_col, 0, BORDERSIZE);

        ((render_workspace_t)(render_workspace_hook->m_pOriginal))(
            g_pHyprRenderer.get(),
            monitor,
            start_workspace,
            &time,
            render_box
        );
    }

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;
    const PHLWINDOW dragged_window = g_pInputManager->currentlyDraggedWindow.lock();
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
