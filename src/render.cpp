#include "render.hpp"

#include <ctime>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/RendererHintsPassElement.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "globals.hpp"
#include "types.hpp"

// Note: box is relative to (0, 0), not monitor
void render_window_at_box(PHLWINDOW window, PHLMONITOR monitor, timespec* time, CBox box) {
    if (!window || !monitor || !time)
        return;

    box.x -= monitor->m_position.x;
    box.y -= monitor->m_position.y;

    const float scale = box.w / window->m_realSize->value().x;
    const Vector2D transform =
        (monitor->m_position - window->m_realPosition->value() + box.pos() / scale)
        * monitor->m_scale;

    SRenderModifData data {};
    data.modifs.push_back({SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE, transform});
    data.modifs.push_back({SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE, scale});
    g_pHyprRenderer->m_renderPass.add(
        makeShared<CRendererHintsPassElement>(CRendererHintsPassElement::SData {data})
    );

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

    g_pHyprRenderer->m_renderPass.add(makeShared<CRendererHintsPassElement>(
        CRendererHintsPassElement::SData {SRenderModifData {}}
    ));
}
