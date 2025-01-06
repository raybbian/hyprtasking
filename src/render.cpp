#include "render.hpp"

#include <ctime>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "globals.hpp"
#include "types.hpp"

// Note: box is relative to (0, 0), not monitor
void render_window_at_box(PHLWINDOW window, PHLMONITOR monitor, timespec* time, CBox box) {
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

    CBox window_box = view->layout->get_global_window_box(window, window->workspaceID());
    if (window_box.empty())
        return false;

    return !window_box.intersection(monitor->logicalBox()).empty();
}
