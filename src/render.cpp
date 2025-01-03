#include <cassert>
#include <ctime>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/pass/RendererHintsPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "config.hpp"
#include "globals.hpp"
#include "overview.hpp"
#include "pass/no_simplify_element.hpp"
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

    SRenderModifData modifs {};
    modifs.modifs.push_back({SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE, transform});
    modifs.modifs.push_back({SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE, scale});

    g_pHyprRenderer->m_sRenderPass.add(
        makeShared<CRendererHintsPassElement>(CRendererHintsPassElement::SData {modifs})
    );

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

    g_pHyprRenderer->m_sRenderPass.add(makeShared<CRendererHintsPassElement>(
        CRendererHintsPassElement::SData {SRenderModifData {}}
    ));

    Debug::log(LOG, "[Hyprtasking] Trying to render drag window");
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

    CBox window_box = view->get_global_window_box(window);
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

void HTView::init_overview_images() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const int ROWS = HTConfig::rows();
    const Vector2D GAPS = gaps();
    // const Vector2D final_size = ((monitor->vecPixelSize - GAPS * (ROWS + 1)) / ROWS).round();
    const Vector2D final_size = monitor->vecPixelSize;

    overview_images.resize(ROWS * ROWS);

    for (const auto& [ws_id, ws_layout] : overview_layout) {
        int ind = ws_layout.row * ROWS + ws_layout.col;

        overview_images[ind].workspace_id = ws_id;

        if (overview_images[ind].fb.m_vSize != final_size) {
            overview_images[ind].fb.release();
            overview_images[ind]
                .fb.alloc(final_size.x, final_size.y, monitor->output->state->state().drmFormat);
        }
    }
}

void HTView::pre_render() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    static const std::string CLEAR_PASS_ELEMENT_NAME = "CClearPassElement";

    timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);

    build_overview_layout();
    init_overview_images();

    const CBox local_mon_box = {{0, 0}, monitor->vecPixelSize};
    const CBox global_mon_box = {monitor->vecPosition, monitor->vecPixelSize};

    // Do a dance with active workspaces: Hyprland will only properly render the
    // current active one so make the workspace active before rendering it, etc
    const PHLWORKSPACE start_workspace = monitor->activeWorkspace;
    start_workspace->startAnim(false, false, true);
    start_workspace->m_bVisible = false;

    for (HTWorkspaceImage& image : overview_images) {
        const WORKSPACEID ws_id = image.workspace_id;
        const HTWorkspace& ws_layout = overview_layout[ws_id];

        // Could be nullptr, in which we render only layers
        const PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(ws_id);

        // If not going to be displayed on the screen, we can skip its rendering
        const CBox global_box = {ws_layout.box.pos() + monitor->vecPosition, ws_layout.box.size()};
        if (global_box.intersection(global_mon_box).empty())
            continue;

        CRegion fake_damage {0, 0, INT16_MAX, INT16_MAX};
        // CRegion fake_damage = ws_layout.box;
        g_pHyprRenderer
            ->beginRender(monitor, fake_damage, RENDER_MODE_FULL_FAKE, nullptr, &image.fb);

        g_pHyprRenderer->m_sRenderPass.add(makeShared<HTDisableSimplification>());

        const CBox render_box = {{ws_layout.box.pos() / scale.value()}, ws_layout.box.size()};

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

        g_pHyprRenderer->endRender();
    }

    monitor->activeWorkspace = start_workspace;
    start_workspace->startAnim(true, false, true);
    start_workspace->m_bVisible = true;
}

void HTView::render() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const int ROWS = HTConfig::rows();
    const Vector2D GAPS = gaps();
    const Vector2D final_size = (monitor->vecPixelSize - GAPS * (ROWS + 1)) / ROWS;

    timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);

    CBox local_mon_box = {{0, 0}, monitor->vecPixelSize};
    const CBox global_mon_box = {monitor->vecPosition, monitor->vecPixelSize};

    g_pHyprRenderer->damageMonitor(monitor);
    g_pHyprRenderer->m_sRenderPass.add(makeShared<CClearPassElement>(
        CClearPassElement::SClearData {CHyprColor {HTConfig::bg_color()}.stripA()}
    ));
    g_pHyprRenderer->m_sRenderPass.add(makeShared<HTDisableSimplification>());

    for (HTWorkspaceImage& image : overview_images) {
        const WORKSPACEID ws_id = image.workspace_id;
        const HTWorkspace& ws_layout = overview_layout[ws_id];

        // Render active last
        if (ws_id == monitor->activeWorkspaceID())
            continue;

        // If not going to be displayed on the screen, we can skip its rendering
        const CBox global_box = {ws_layout.box.pos() + monitor->vecPosition, ws_layout.box.size()};
        if (global_box.intersection(global_mon_box).empty())
            continue;

        g_pHyprRenderer->m_sRenderPass.add(makeShared<CTexPassElement>(
            CTexPassElement::SRenderData {image.fb.getTexture(), local_mon_box, 1.0, ws_layout.box}
        ));
    }

    // Render active
    for (HTWorkspaceImage& image : overview_images) {
        const WORKSPACEID ws_id = image.workspace_id;
        const HTWorkspace& ws_layout = overview_layout[ws_id];

        // Skip non-active
        if (ws_id != monitor->activeWorkspaceID())
            continue;

        g_pHyprRenderer->m_sRenderPass.add(makeShared<CTexPassElement>(
            CTexPassElement::SRenderData {image.fb.getTexture(), local_mon_box, 1.0, ws_layout.box}
        ));
    }

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;
    const PHLWINDOW dragged_window = g_pInputManager->currentlyDraggedWindow.lock();
    if (dragged_window == nullptr)
        return;
    // Render the window at the scale of the dragged view
    const Vector2D window_sz = dragged_window->m_vRealSize.value()
        * cursor_view->scale.value(); // divide by ROWS (use cursor's view)
    const CBox window_box = {
        g_pInputManager->getMouseCoordsInternal() - window_sz / 2.f
            + ht_manager->dragged_window_offset.value(),
        window_sz
    };
    if (!window_box.intersection(monitor->logicalBox()).empty())
        render_window_at_box(dragged_window, monitor, &time, window_box);
}
