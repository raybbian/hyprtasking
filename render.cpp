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

    workspaceBoxes.clear();

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

    // TODO: is this inefficient?
    g_pHyprRenderer->damageMonitor(pMonitor);
    g_pHyprOpenGL->m_RenderData.pCurrentMonData->blurFBShouldRender = true;
    CBox viewBox = {{0, 0}, pMonitor->vecPixelSize};
    g_pHyprOpenGL->renderRect(&viewBox, CHyprColor{0, 0, 0, 1.0});

    // Do a dance with active workspaces: Hyprland will only properly render the
    // current active one so make the workspace active before rendering it, etc
    const PHLWORKSPACE startWorkspace = pMonitor->activeWorkspace;
    startWorkspace->startAnim(false, false, true);
    startWorkspace->m_bVisible = false;

    Vector2D workspaceSize = pMonitor->vecPixelSize / ROWS;
    for (size_t i = 0; i < ROWS; i++) {
        for (size_t j = 0; j < ROWS; j++) {
            size_t ind = j * ROWS + i;
            const PHLWORKSPACE pWorkspace =
                ind < workspaces.size()
                    ? g_pCompositor->getWorkspaceByID(workspaces[ind])
                    : nullptr;

            // renderModif translation used by renderWorkspace is weird so need
            // to scale the translation up as well
            CBox actualBox = {{i * workspaceSize.x, j * workspaceSize.y},
                              workspaceSize};
            CBox curBox = {{actualBox.pos() * ROWS}, actualBox.size()};

            if (pWorkspace != nullptr) {
                pMonitor->activeWorkspace = pWorkspace;
                pWorkspace->startAnim(true, false, true);
                pWorkspace->m_bVisible = true;

                ((tRenderWorkspace)(g_pRenderWorkspaceHook->m_pOriginal))(
                    g_pHyprRenderer.get(), pMonitor, pWorkspace, &time, curBox);

                pWorkspace->startAnim(false, false, true);
                pWorkspace->m_bVisible = false;

                workspaceBoxes.emplace_back(pWorkspace->m_iID, actualBox);
            } else {
                // If pWorkspace is null, then just render the layers
                ((tRenderWorkspace)(g_pRenderWorkspaceHook->m_pOriginal))(
                    g_pHyprRenderer.get(), pMonitor, pWorkspace, &time, curBox);
            }
        }
    }

    pMonitor->activeWorkspace = startWorkspace;
    startWorkspace->startAnim(true, false, true);
    startWorkspace->m_bVisible = true;
}
