#include <ctime>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Box.hpp>

#include "overview.hpp"

CHyprtaskingView::CHyprtaskingView(MONITORID inMonitorID) {
    monitorID = inMonitorID;
    m_bActive = false;
    m_bClosing = false;

    m_vOffset.create(
        {0, 0}, g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
        AVARDAMAGE_NONE);
    m_fScale.create(1.f,
                    g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
                    AVARDAMAGE_NONE);
}

void CHyprtaskingView::show() {
    if (m_bActive)
        return;
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    m_bActive = true;

    // Generate workspace boxes for non-animating configuration
    generateWorkspaceBoxes(false);

    const CBox wsBox = workspaceBoxes[pMonitor->activeWorkspaceID()];

    m_fScale.setValueAndWarp(1.);
    m_vOffset.setValueAndWarp(-wsBox.pos() / wsBox.size() *
                              pMonitor->vecPixelSize);

    m_fScale = wsBox.w / pMonitor->vecPixelSize.x; // 1 / ROWS
    m_vOffset = {0, 0};

    g_pHyprRenderer->damageMonitor(pMonitor);
    g_pCompositor->scheduleFrameForMonitor(pMonitor);
}

void CHyprtaskingView::hide() {
    if (m_bClosing || !m_bActive)
        return;
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    m_bClosing = true;

    // Generate workspace boxes for non-animating
    generateWorkspaceBoxes(false);

    const CBox wsBox = workspaceBoxes[pMonitor->activeWorkspaceID()];

    m_fScale = 1.;
    m_vOffset = -wsBox.pos() / wsBox.size() * pMonitor->vecPixelSize;

    m_fScale.setCallbackOnEnd([this](void *) {
        m_bActive = false;
        m_bClosing = false;
    });

    g_pHyprRenderer->damageMonitor(pMonitor);
    g_pCompositor->scheduleFrameForMonitor(pMonitor);
}

Vector2D
CHyprtaskingView::mapGlobalPositionToWsGlobal(Vector2D pos,
                                              WORKSPACEID workspaceID) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return {};

    CBox workspaceBox = getWorkspaceBoxFromID(workspaceID);
    if (workspaceBox.empty())
        return {};

    pos *= pMonitor->scale;
    pos -= workspaceBox.pos();
    pos /= m_fScale.value(); // multiply by ROWS
    pos += pMonitor->vecPosition;
    pos /= pMonitor->scale;
    return pos;
}

Vector2D
CHyprtaskingView::mapWsGlobalPositionToGlobal(Vector2D pos,
                                              WORKSPACEID workspaceID) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return {};

    CBox workspaceBox = getWorkspaceBoxFromID(workspaceID);
    if (workspaceBox.empty())
        return {};

    pos *= pMonitor->scale;
    pos -= pMonitor->vecPosition;
    pos *= m_fScale.value(); // divide by ROWS
    pos += workspaceBox.pos();
    pos /= pMonitor->scale;
    return pos;
}

WORKSPACEID CHyprtaskingView::getWorkspaceIDFromVector(Vector2D pos) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return WORKSPACE_INVALID;
    Vector2D relPos = (pos - pMonitor->vecPosition) * pMonitor->scale;
    for (const auto &[id, box] : workspaceBoxes)
        if (box.containsPoint(relPos))
            return id;
    return WORKSPACE_INVALID;
}

CBox CHyprtaskingView::getWorkspaceBoxFromID(WORKSPACEID workspaceID) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return {};
    if (workspaceBoxes.count(workspaceID)) {
        CBox oBox = workspaceBoxes[workspaceID];
        // NOTE: not scaled by monitor size
        return {pMonitor->vecPosition + oBox.pos(), oBox.size()};
    }
    return {};
}

PHLMONITOR CHyprtaskingView::getMonitor() {
    return g_pCompositor->getMonitorFromID(monitorID);
}

CHyprtaskingManager::CHyprtaskingManager() {
    dragWindowOffset.create(
        {0, 0}, g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
        AVARDAMAGE_NONE);
}

PHTVIEW CHyprtaskingManager::getViewFromMonitor(PHLMONITOR pMonitor) {
    if (pMonitor == nullptr)
        return nullptr;
    for (PHTVIEW pView : m_vViews) {
        if (pView == nullptr)
            continue;
        if (pView->getMonitor() != pMonitor)
            continue;
        return pView;
    }
    return nullptr;
}

void CHyprtaskingManager::show() {
    for (auto view : m_vViews) {
        if (view == nullptr)
            continue;
        view->show();
    }
}

void CHyprtaskingManager::hide() {
    const PHLMONITOR pMonitor = g_pCompositor->getMonitorFromCursor();
    for (const auto &view : m_vViews) {
        if (view == nullptr)
            continue;
        if (view->getMonitor() == pMonitor) {
            // If this view is monitor view, change to workspace the mouse is on
            const Vector2D mouseCoords =
                g_pInputManager->getMouseCoordsInternal();
            const WORKSPACEID workspaceID =
                view->getWorkspaceIDFromVector(mouseCoords);
            PHLWORKSPACE pWorkspace =
                g_pCompositor->getWorkspaceByID(workspaceID);

            if (pWorkspace == nullptr && workspaceID != WORKSPACE_INVALID) {
                pWorkspace = g_pCompositor->createNewWorkspace(workspaceID,
                                                               pMonitor->ID);

                Debug::log(LOG, "[Hyprtasking] Creating new workspace {}",
                           workspaceID);
            }

            if (pWorkspace != nullptr) {
                pMonitor->changeWorkspace(pWorkspace, true);
                pWorkspace->startAnim(true, false, true);
                pWorkspace->m_bVisible = true;
            }
        }
        view->hide();
    }
}

void CHyprtaskingManager::reset() { m_vViews.clear(); }

bool CHyprtaskingManager::isActive() {
    for (const auto &view : m_vViews) {
        if (view == nullptr)
            continue;
        if (view->m_bActive)
            return true;
    }
    return false;
}
