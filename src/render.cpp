#include <ctime>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "globals.hpp"
#include "overview.hpp"
#include "types.hpp"

// Note: box is relative to (0, 0), not monitor
static void renderWindowAtBox(PHLWINDOW pWindow, PHLMONITOR pMonitor,
                              timespec *time, CBox box) {
    if (!pWindow || !pMonitor || !time)
        return;

    box.x -= pMonitor->vecPosition.x;
    box.y -= pMonitor->vecPosition.y;

    const float scale = box.w / pWindow->m_vRealSize.value().x;
    const Vector2D transform =
        (pMonitor->vecPosition - pWindow->m_vRealPosition.value() +
         box.pos() / scale) *
        pMonitor->scale;

    const bool oRenderModifEnabled =
        g_pHyprOpenGL->m_RenderData.renderModif.enabled;

    g_pHyprOpenGL->m_RenderData.renderModif.modifs.push_back(
        {SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE, transform});
    g_pHyprOpenGL->m_RenderData.renderModif.modifs.push_back(
        {SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE, scale});
    g_pHyprOpenGL->m_RenderData.renderModif.enabled = true;

    g_pHyprRenderer->damageWindow(pWindow);
    ((tRenderWindow)g_pRenderWindow)(g_pHyprRenderer.get(), pWindow, pMonitor,
                                     time, true, RENDER_PASS_MAIN, false,
                                     false);

    g_pHyprOpenGL->m_RenderData.renderModif.enabled = oRenderModifEnabled;
    g_pHyprOpenGL->m_RenderData.renderModif.modifs.pop_back();
    g_pHyprOpenGL->m_RenderData.renderModif.modifs.pop_back();
}

void CHyprtaskingView::generateWorkspaceBoxes(bool useAnimModifs) {
    static long *const *PROWS =
        (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(
            PHANDLE, "plugin:hyprtasking:rows")
            ->getDataStaticPtr();
    const int ROWS = **PROWS;

    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    const PHLMONITOR oMonitor = g_pCompositor->m_pLastMonitor.lock();
    g_pCompositor->setActiveMonitor(pMonitor);

    workspaceBoxes.clear();

    const float scale = useAnimModifs ? m_fScale.value() : 1.f / ROWS;
    const Vector2D offset =
        (useAnimModifs ? m_vOffset.value() : Vector2D{0, 0});
    const Vector2D workspaceSize = pMonitor->vecPixelSize * scale;

    for (int i = 0; i < ROWS; i++) {
        for (int j = 0; j < ROWS; j++) {
            int ind = j * ROWS + i + 1;
            const WORKSPACEID workspaceID =
                getWorkspaceIDNameFromString(std::format("r~{}", ind)).id;
            CBox actualBox = {{i * workspaceSize.x + offset.x,
                               j * workspaceSize.y + offset.y},
                              workspaceSize};
            workspaceBoxes[workspaceID] = actualBox;
        }
    }

    if (oMonitor != nullptr)
        g_pCompositor->setActiveMonitor(oMonitor);
}

void CHyprtaskingView::render() {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);

    g_pHyprRenderer->damageMonitor(pMonitor);
    g_pHyprOpenGL->m_RenderData.pCurrentMonData->blurFBShouldRender = true;
    CBox viewBox = {{0, 0}, pMonitor->vecPixelSize};
    g_pHyprOpenGL->renderRect(&viewBox, CHyprColor{0, 0, 0, 1.0});

    // Do a dance with active workspaces: Hyprland will only properly render the
    // current active one so make the workspace active before rendering it, etc
    const PHLWORKSPACE startWorkspace = pMonitor->activeWorkspace;
    startWorkspace->startAnim(false, false, true);
    startWorkspace->m_bVisible = false;

    generateWorkspaceBoxes();

    for (const auto &[workspaceID, actualBox] : workspaceBoxes) {
        // Could be nullptr, in which we render only layers
        const PHLWORKSPACE pWorkspace =
            g_pCompositor->getWorkspaceByID(workspaceID);

        // renderModif translation used by renderWorkspace is weird so need
        // to scale the translation up as well
        CBox curBox = {{actualBox.pos() / m_fScale.value()}, actualBox.size()};

        // render active one last
        if (pWorkspace == startWorkspace && startWorkspace != nullptr)
            continue;

        if (pWorkspace != nullptr) {
            pMonitor->activeWorkspace = pWorkspace;
            pWorkspace->startAnim(true, false, true);
            pWorkspace->m_bVisible = true;

            ((tRenderWorkspace)(g_pRenderWorkspaceHook->m_pOriginal))(
                g_pHyprRenderer.get(), pMonitor, pWorkspace, &time, curBox);

            pWorkspace->startAnim(false, false, true);
            pWorkspace->m_bVisible = false;

            workspaceBoxes[pWorkspace->m_iID] = actualBox;
        } else {
            // If pWorkspace is null, then just render the layers
            ((tRenderWorkspace)(g_pRenderWorkspaceHook->m_pOriginal))(
                g_pHyprRenderer.get(), pMonitor, pWorkspace, &time, curBox);
            workspaceBoxes[workspaceID] = actualBox;
        }
    }

    pMonitor->activeWorkspace = startWorkspace;
    startWorkspace->startAnim(true, false, true);
    startWorkspace->m_bVisible = true;

    // Render active workspace
    if (startWorkspace != nullptr) {
        CBox actualBox = workspaceBoxes[startWorkspace->m_iID];
        CBox curBox = {{actualBox.pos() / m_fScale.value()}, actualBox.size()};

        ((tRenderWorkspace)(g_pRenderWorkspaceHook->m_pOriginal))(
            g_pHyprRenderer.get(), pMonitor, startWorkspace, &time, curBox);
    }

    const PHTVIEW cursorView = g_pHyprtasking->getViewFromCursor();
    if (cursorView == nullptr)
        return;
    const PHLWINDOW dragWindow = g_pInputManager->currentlyDraggedWindow.lock();
    if (dragWindow == nullptr)
        return;
    const Vector2D dragSize =
        dragWindow->m_vRealSize.value() *
        cursorView->m_fScale.value(); // divide by ROWS (use cursor's view)
    const CBox dragBox = {g_pInputManager->getMouseCoordsInternal() -
                              dragSize / 2.f +
                              g_pHyprtasking->dragWindowOffset.value(),
                          dragSize};
    if (!dragBox.intersection(pMonitor->logicalBox()).empty())
        renderWindowAtBox(dragWindow, pMonitor, &time, dragBox);
}
