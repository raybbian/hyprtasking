#include <ctime>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "globals.hpp"
#include "overview.hpp"

#define LAYER_BACKGROUND 0
#define LAYER_BOTTOM 1
#define LAYER_OVERLAY 2
#define LAYER_TOP 3

void CHyprtaskingView::renderWindow(PHLWINDOW pWindow, timespec *time) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    const auto oWorkspace = pWindow->m_pWorkspace;
    pWindow->m_pWorkspace = pMonitor->activeWorkspace;

    g_pHyprRenderer->damageWindow(pWindow);
    ((tRenderWindow)g_pRenderWindow)(g_pHyprRenderer.get(), pWindow, pMonitor,
                                     time, true, RENDER_PASS_MAIN, false,
                                     false);

    pWindow->m_pWorkspace = oWorkspace;
}

void CHyprtaskingView::renderLayer(PHLLS pLayer, timespec *time) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    ((tRenderLayer)g_pRenderLayer)(g_pHyprRenderer.get(), pLayer, pMonitor,
                                   time, false);
}

void CHyprtaskingView::renderWorkspace(PHLWORKSPACE pWorkspace, timespec *time,
                                       const CBox &geometry) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    float scale = (float)geometry.width / pMonitor->vecPixelSize.x;
    Vector2D translate = Vector2D{geometry.x, geometry.y} / scale;

    bool oRenderModifEnabled = g_pHyprOpenGL->m_RenderData.renderModif.enabled;

    g_pHyprOpenGL->m_RenderData.renderModif.modifs.emplace_back(
        std::make_pair<>(
            SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE,
            translate));
    g_pHyprOpenGL->m_RenderData.renderModif.modifs.emplace_back(
        std::make_pair<>(SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE,
                         scale));
    g_pHyprOpenGL->m_RenderData.renderModif.enabled = true;

    // Render bottom layers
    for (auto &pLayer : pMonitor->m_aLayerSurfaceLayers[LAYER_BACKGROUND]) {
        renderLayer(pLayer.lock(), time);
    }
    for (auto &pLayer : pMonitor->m_aLayerSurfaceLayers[LAYER_BOTTOM]) {
        renderLayer(pLayer.lock(), time);
    }

    // Render tiled, then floating, then active
    for (auto &pWindow : g_pCompositor->m_vWindows) {
        if (pWindow == nullptr || pWindow->m_pWorkspace != pWorkspace)
            continue;
        if (pWindow->m_bIsFloating)
            continue;
        renderWindow(pWindow, time);
    }
    for (auto &pWindow : g_pCompositor->m_vWindows) {
        if (pWindow == nullptr || pWindow->m_pWorkspace != pWorkspace)
            continue;
        if (!pWindow->m_bIsFloating)
            continue;
        if (pWorkspace->getLastFocusedWindow() == pWindow)
            continue;
        renderWindow(pWindow, time);
    }
    if (auto pWindow = pWorkspace->getLastFocusedWindow()) {
        if (pWindow->m_bIsFloating) {
            renderWindow(pWindow, time);
        }
    }

    // Render top layers
    for (auto &pLayer : pMonitor->m_aLayerSurfaceLayers[LAYER_OVERLAY]) {
        renderLayer(pLayer.lock(), time);
    }
    for (auto &pLayer : pMonitor->m_aLayerSurfaceLayers[LAYER_TOP]) {
        renderLayer(pLayer.lock(), time);
    }

    g_pHyprOpenGL->m_RenderData.renderModif.modifs.pop_back();
    g_pHyprOpenGL->m_RenderData.renderModif.modifs.pop_back();
    g_pHyprOpenGL->m_RenderData.renderModif.enabled = oRenderModifEnabled;
}

void CHyprtaskingView::render() {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

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
    pMonitor->addDamage(&viewBox);

    const PHLWORKSPACE startWorkspace = pMonitor->activeWorkspace;
    startWorkspace->startAnim(false, false, true);
    startWorkspace->m_bVisible = false;

    double workspaceX = 0.0;
    double workspaceY = 0.0;
    double scale = 0.5;
    double workspaceW = pMonitor->vecPixelSize.x * scale;
    double workspaceH = pMonitor->vecPixelSize.y * scale;
    for (const WORKSPACEID wsID : workspaces) {
        const PHLWORKSPACE pWorkspace = g_pCompositor->getWorkspaceByID(wsID);
        if (pWorkspace == nullptr || pMonitor == nullptr)
            continue;

        pMonitor->activeWorkspace = pWorkspace;
        pWorkspace->startAnim(true, false, true);
        pWorkspace->m_bVisible = true;

        CBox curBox{workspaceX, workspaceY, workspaceW, workspaceH};
        ((tRenderWorkspace)(g_pRenderWorkspaceHook->m_pOriginal))(
            g_pHyprRenderer.get(), pMonitor, pWorkspace, &time, curBox);

        pWorkspace->startAnim(false, false, true);
        pWorkspace->m_bVisible = false;

        workspaceX += workspaceW / scale;
    }

    pMonitor->activeWorkspace = startWorkspace;
    startWorkspace->startAnim(true, false, true);
    startWorkspace->m_bVisible = true;
}
