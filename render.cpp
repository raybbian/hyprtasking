#include <ctime>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "globals.hpp"
#include "overview.hpp"

void CHyprtaskingView::render() {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    g_pHyprRenderer->makeEGLCurrent();

    std::vector<WORKSPACEID> workspaces;
    for (auto &ws : g_pCompositor->m_vWorkspaces) {
        if (ws == nullptr)
            continue;
        if (ws->m_pMonitor->ID != monitorID)
            continue;
        // ignore special workspaces for now
        if (ws->m_iID < 1)
            continue;
        workspaces.push_back(ws->m_iID);
    }
    std::sort(workspaces.begin(), workspaces.end());

    timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);

    CBox viewBox = {{0, 0}, pMonitor->vecPixelSize};
    g_pHyprOpenGL->renderRect(&viewBox, CHyprColor{0, 0, 0, 1.0});

    const PHLWORKSPACE startWorkspace = pMonitor->activeWorkspace;
    startWorkspace->m_bVisible = false;

    double workspaceX = 0.0;
    double workspaceY = 0.0;
    double scale = 0.5;
    double workspaceW = pMonitor->vecPixelSize.x * scale;
    double workspaceH = pMonitor->vecPixelSize.y * scale;
    for (const WORKSPACEID wsID : workspaces) {
        const PHLWORKSPACE pWorkspace = g_pCompositor->getWorkspaceByID(wsID);
        if (pWorkspace == nullptr)
            continue;
        CBox curBox{workspaceX, workspaceY, workspaceW, workspaceH};

        g_pHyprOpenGL->clear(CHyprColor{0, 0, 0, 1.0});

        pMonitor->activeWorkspace = pWorkspace;
        pWorkspace->m_bVisible = true;

        ((tRenderWorkspace)g_pRenderWorkspaceHook->m_pOriginal)(
            g_pHyprRenderer.get(), pMonitor, pWorkspace, &time, curBox);

        pWorkspace->m_bVisible = false;

        workspaceX += workspaceW / scale;
    }

    pMonitor->activeWorkspace = startWorkspace;
    startWorkspace->m_bVisible = true;
}
