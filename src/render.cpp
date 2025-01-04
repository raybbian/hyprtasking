#include <ctime>
#include <hyprland/src/Compositor.hpp>
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

Vector2D HTView::gaps() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};
    const Vector2D GAPS = {
        (double)HTConfig::gap_size(),
        (double)HTConfig::gap_size() * monitor->vecPixelSize.y / monitor->vecPixelSize.x
    };
    return GAPS.round();
}

void HTView::build_overview_layout(bool use_anim_modifs) {
    const int ROWS = HTConfig::rows();

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const Vector2D GAPS = gaps();

    const PHLMONITOR last_monitor = g_pCompositor->m_pLastMonitor.lock();
    g_pCompositor->setActiveMonitor(monitor);

    overview_layout.clear();

    const float use_scale = use_anim_modifs
        ? scale.value()
        : ((monitor->vecPixelSize.x - (ROWS + 1) * GAPS.x) / ROWS) / monitor->vecPixelSize.x;

    const Vector2D use_offset = (use_anim_modifs ? offset.value() : Vector2D {0, 0});
    const Vector2D ws_sz = monitor->vecPixelSize * use_scale;

    for (int i = 0; i < ROWS; i++) {
        for (int j = 0; j < ROWS; j++) {
            int ind = i * ROWS + j + 1;
            const WORKSPACEID ws_id = getWorkspaceIDNameFromString(std::format("r~{}", ind)).id;
            CBox ws_box = {Vector2D {j, i} * (ws_sz + GAPS) + GAPS + use_offset, ws_sz};
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
