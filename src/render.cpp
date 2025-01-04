#include <ctime>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "config.hpp"
#include "globals.hpp"
#include "overview.hpp"
#include "types.hpp"

// Note: box is relative to (0, 0), not monitor
static void render_window_at_box(PHLWINDOW window, PHLMONITOR monitor, timespec* time, CBox box) {
    if (!window || !monitor || !time)
        return;

    box.x -= monitor->vecPosition.x;
    box.y -= monitor->vecPosition.y;

    const float scale = box.w / window->m_vRealSize.value().x;
    const Vector2D transform =
        (monitor->vecPosition - window->m_vRealPosition.value() + box.pos() / scale)
        * monitor->scale;

    const bool o_render_modif_enabled = g_pHyprOpenGL->m_RenderData.renderModif.enabled;

    g_pHyprOpenGL->m_RenderData.renderModif.modifs.push_back(
        {SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE, transform}
    );
    g_pHyprOpenGL->m_RenderData.renderModif.modifs.push_back(
        {SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE, scale}
    );
    g_pHyprOpenGL->m_RenderData.renderModif.enabled = true;

    g_pHyprRenderer->damageWindow(window);
    ((render_window_t)render_window)(
        g_pHyprRenderer.get(),
        window,
        monitor,
        time,
        true,
        RENDER_PASS_MAIN,
        false,
        false
    );

    g_pHyprOpenGL->m_RenderData.renderModif.enabled = o_render_modif_enabled;
    g_pHyprOpenGL->m_RenderData.renderModif.modifs.pop_back();
    g_pHyprOpenGL->m_RenderData.renderModif.modifs.pop_back();
}

bool HTManager::should_render_window(PHLWINDOW window, PHLMONITOR monitor) {
    if (window == nullptr || monitor == nullptr)
        return false;

    if (window == g_pInputManager->currentlyDraggedWindow.lock())
        return false;

    PHLWORKSPACE workspace = window->m_pWorkspace;
    PHTVIEW view = get_view_from_monitor(monitor);
    if (workspace == nullptr || view == nullptr)
        return false;

    CBox window_box = view->get_global_window_box(window, window->workspaceID());
    if (window_box.empty())
        return false;

    return !window_box.intersection(monitor->logicalBox()).empty();
}

CBox HTView::calculate_ws_box(int x, int y, int override) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    const int ROWS = HTConfig::rows();
    const double GAP_SIZE = HTConfig::gap_size() * monitor->scale;
    const Vector2D GAPS = {GAP_SIZE, GAP_SIZE};

    double render_x = monitor->vecPixelSize.x - GAPS.x * (ROWS + 1);
    double render_y = monitor->vecPixelSize.y - GAPS.y * (ROWS + 1);
    const double mon_aspect = monitor->vecPixelSize.x / monitor->vecPixelSize.y;
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
    if (override == -1) {
        use_scale = 1;
        use_offset = Vector2D {0, 0};
    } else if (override == 1) {
        use_scale = (render_x / ROWS) / monitor->vecPixelSize.x;
        use_offset = Vector2D {0, 0};
    }

    const Vector2D ws_sz = monitor->vecPixelSize * use_scale;
    return CBox {Vector2D {x, y} * (ws_sz + GAPS) + GAPS + use_offset + start_offset, ws_sz};
}

void HTView::build_overview_layout(int override) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const int ROWS = HTConfig::rows();

    const PHLMONITOR last_monitor = g_pCompositor->m_pLastMonitor.lock();
    g_pCompositor->setActiveMonitor(monitor);

    overview_layout.clear();

    for (int i = 0; i < ROWS; i++) {
        for (int j = 0; j < ROWS; j++) {
            int ind = i * ROWS + j + 1;
            const WORKSPACEID ws_id = getWorkspaceIDNameFromString(std::format("r~{}", ind)).id;
            const CBox ws_box = calculate_ws_box(j, i, override);
            overview_layout[ws_id] = HTWorkspace {i, j, ws_box};
        }
    }

    if (last_monitor != nullptr)
        g_pCompositor->setActiveMonitor(last_monitor);
}

void HTView::render() {
    const PHLMONITOR monitor = get_monitor();
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
    CBox monitor_box = {{0, 0}, monitor->vecPixelSize};
    g_pHyprOpenGL->renderRect(&monitor_box, CHyprColor {HTConfig::bg_color()}.stripA());

    // Do a dance with active workspaces: Hyprland will only properly render the
    // current active one so make the workspace active before rendering it, etc
    const PHLWORKSPACE start_workspace = monitor->activeWorkspace;
    start_workspace->startAnim(false, false, true);
    start_workspace->m_bVisible = false;

    build_overview_layout();

    const WORKSPACEID exit_workspace_id = get_exit_workspace_id(false);

    CBox global_mon_box = {monitor->vecPosition, monitor->vecPixelSize};
    for (const auto& [ws_id, ws_layout] : overview_layout) {
        // Could be nullptr, in which we render only layers
        const PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(ws_id);

        // renderModif translation used by renderWorkspace is weird so need
        // to scale the translation up as well
        CBox render_box = {{ws_layout.box.pos() / scale.value()}, ws_layout.box.size()};

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
        CBox render_box = {{ws_box.pos() / scale.value()}, ws_box.size()};

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
                                .scale(cursor_view->scale.value())
                                .translate(mouse_coords);
    if (!window_box.intersection(monitor->logicalBox()).empty())
        render_window_at_box(dragged_window, monitor, &time, window_box);
}
